#include "od.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const char *roots[2]={"/usr/share/lilsmoke/opendingux/apps","/storage/opendingux/apps"};
static const char *cache_root="/storage/opendingux/cache";
static const char *home_root="/persist/opendingux/home";
struct entry {
	char id[68],source[OD_PATH],desktop[OD_PATH];
	struct od_desktop d;
	int opk,builtin;
	unsigned nodes;
	uint64_t expanded;
};
static struct entry catalog[OD_CATALOG];
static unsigned count;
static int lock_root=-1,lock_dir=-1;
static void release_lock(void)
{
	struct stat a,b;
	if(lock_dir>=0&&lock_root>=0) {
		unlinkat(lock_dir,"owner",0);
		if(!fstat(lock_dir,&a)&&!fstatat(lock_root,".lock",&b,AT_SYMLINK_NOFOLLOW)&&
		   a.st_dev==b.st_dev&&a.st_ino==b.st_ino)
			unlinkat(lock_root,".lock",AT_REMOVEDIR);
		close(lock_dir);close(lock_root);
	}
	lock_dir=-1;lock_root=-1;
}
static void interrupted(int sig)
{
	release_lock();
	_exit(128+sig);
}
static int acquire_lock(int root,char *reason)
{
	int owner=-1,n;
	char pid[32];
	struct sigaction action;
	if(mkdirat(root,".lock",0700)) {
		int dir=openat(root,".lock",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
		struct stat st;
		if(dir<0)return od_fail(reason,"OPK cache lock is unsafe or unavailable");
		owner=openat(dir,"owner",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
		if(owner>=0&&!fstat(owner,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<31&&
		   !od_fd_read(&owner,0,pid,(size_t)st.st_size)) {
			char *end;long value;
			pid[st.st_size]=0;value=strtol(pid,&end,10);
			if(!*end&&value>0&&value<=INT32_MAX&&kill((pid_t)value,0)&&errno==ESRCH)
				od_fail(reason,"Stale OPK cache lock; remove cache/.lock while runtime is stopped");
		}
		if(owner>=0)close(owner);
		close(dir);
		return od_fail(reason,"OPK cache is busy; another runtime owns its mkdir lock");
	}
	lock_root=dup(root);
	lock_dir=openat(root,".lock",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
	if(lock_root<0||lock_dir<0) {
		if(lock_root>=0)close(lock_root);
		if(lock_dir>=0)close(lock_dir);
		lock_root=lock_dir=-1;
		unlinkat(root,".lock",AT_REMOVEDIR);
		return od_fail(reason,"Cannot open OPK cache mkdir lock");
	}
	memset(&action,0,sizeof(action));action.sa_handler=interrupted;sigemptyset(&action.sa_mask);
	sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);sigaction(SIGHUP,&action,NULL);
	owner=openat(lock_dir,"owner",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
	n=snprintf(pid,sizeof(pid),"%ld",(long)getpid());
	if(owner<0||od_write(owner,pid,(size_t)n)||fsync(owner)||fsync(lock_dir)) {
		if(owner>=0)close(owner);
		release_lock();return od_fail(reason,"Cannot write OPK cache lock owner");
	}
	close(owner);return 0;
}
static int ends(const char *s,const char *suffix)
{
	size_t a=strlen(s),b=strlen(suffix);
	return a>=b&&!strcmp(s+a-b,suffix);
}
static void identity(struct entry *e)
{
	struct od_sha s;
	od_sha_init(&s);od_sha_update(&s,e->source,strlen(e->source)+1);
	od_sha_update(&s,e->desktop,strlen(e->desktop));
	memcpy(e->id,"od-",3);od_sha_final(&s,e->id+3);
}
static struct entry *new_entry(const char *source,const char *desktop,int opk)
{
	struct entry *e;
	const char *base=strrchr(source,'/');
	if(count==OD_CATALOG)return NULL;
	e=&catalog[count++];memset(e,0,sizeof(*e));
	strcpy(e->source,source);strcpy(e->desktop,desktop);
	od_clean(e->d.name,sizeof(e->d.name),base?base+1:source,strlen(base?base+1:source));
	e->opk=opk;identity(e);return e;
}
static int native_guard(const char *path,char *reason,int *out)
{
	struct stat st;
	struct od_reader r;
	int fd=od_open_path(path,O_RDONLY);
	if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>OD_FILE_MAX||
	   !(st.st_mode&0111)) {
		if(fd>=0)close(fd);
		return od_fail(reason,"Native executable is missing, unsafe or not executable");
	}
	r.ctx=&fd;r.size=(uint64_t)st.st_size;r.read=od_fd_read;
	if(od_elf(&r,reason,1)){close(fd);return -1;}
	if(out)*out=fd;else close(fd);
	return 0;
}
static int executable_path(struct entry *e,char path[OD_PATH])
{
	const char *p=e->d.args[0];
	if(!strncmp(p,"./",2))p+=2;
	if(e->opk) {
		if(!od_relative(p))return od_fail(e->d.reason,"OPK Exec must be relative to the package root");
		strcpy(path,p);return 0;
	}
	if(p[0]=='/') {
		if(strncmp(p,"/usr/lib/lilsmoke/od-emulators/",31))
			return od_fail(e->d.reason,"Native catalog Exec must use the native emulator directory or a relative path");
		strcpy(path,p);return 0;
	}
	if(!od_relative(p))return od_fail(e->d.reason,"Unsafe native Exec path");
	{
		char dir[OD_PATH],*slash;
		strcpy(dir,e->source);slash=strrchr(dir,'/');
		if(!slash)return od_fail(e->d.reason,"Invalid desktop path");
		*slash=0;
		if(od_join(path,dir,p))return od_fail(e->d.reason,"Native executable path is too long");
	}
	return 0;
}
static int guard_entry(struct entry *e,struct od_opk *o)
{
	char path[OD_PATH];
	if(e->d.reason[0])return -1;
	e->builtin=!e->opk&&!strcmp(e->source,"/usr/share/lilsmoke/opendingux/apps/pocketsnes.desktop")&&
		!strcmp(e->d.exec,"/usr/lib/lilsmoke/od-launch pocketsnes %f");
	if(e->builtin) {
		if(access("/usr/lib/lilsmoke/od-launch",X_OK))
			return od_fail(e->d.reason,"PocketSNES launcher is not installed");
		return native_guard("/usr/lib/lilsmoke/od-emulators/pocketsnes",e->d.reason,NULL);
	}
	if(executable_path(e,path))return -1;
	if(o) {
		struct od_reader r;void *token=NULL;
		int result=od_opk_reader(o,path,&r,&token,e->d.reason);
		if(!result)result=od_elf(&r,e->d.reason,1);
		od_opk_reader_free(token);return result;
	}
	return native_guard(path,e->d.reason,NULL);
}
struct metadata {const char *source;unsigned first,nodes;uint64_t bytes;char reason[OD_REASON];};
static int metadata_cb(struct od_opk *o,const char *path,const struct od_reader *r,unsigned mode,void *ctx)
{
	struct metadata *m=ctx;
	unsigned char magic[4];
	(void)o;(void)mode;
	++m->nodes;
	if(!r)return 0;
	m->bytes+=r->size;
	if(ends(path,".desktop")) {
		char buf[OD_META];
		struct entry *e=new_entry(m->source,path,1);
		if(!e)return od_fail(m->reason,"Catalog limit reached");
		if(r->size>OD_META||r->read(r->ctx,0,buf,(size_t)r->size))
			return od_fail(m->reason,"Desktop metadata exceeds bounds or is corrupt");
		od_parse(buf,(size_t)r->size,&e->d);
		if(!e->d.name[0])od_clean(e->d.name,sizeof(e->d.name),path,strlen(path));
	}
	if(r->size>=4 && !r->read(r->ctx,0,magic,4) && !memcmp(magic,"\177ELF",4))
		return od_elf(r,m->reason,0);
	return 0;
}
static void scan_source(const char *source,int is_opk)
{
	struct stat st;
	int fd=od_open_path(source,O_RDONLY);
	if(is_opk) {
		struct od_opk *o;
		struct metadata m;
		unsigned i;
		memset(&m,0,sizeof(m));m.source=source;m.first=count;
		o=fd<0?NULL:od_opk_open(fd,m.reason);
		if(!o&&!m.reason[0])od_fail(m.reason,"OPK cannot be opened safely");
		if(o&&od_opk_walk(o,metadata_cb,&m,m.reason))
			od_fail(m.reason,"OPK metadata traversal failed");
		if(count==m.first) {
			struct entry *e=new_entry(source,"",1);
			if(e)od_fail(e->d.reason,m.reason[0]?m.reason:"OPK has no Desktop Entry");
		}
		for(i=m.first;i<count;++i) {
			catalog[i].expanded=m.bytes;catalog[i].nodes=m.nodes;
			if(m.reason[0])od_fail(catalog[i].d.reason,m.reason);
			else guard_entry(&catalog[i],o);
		}
		od_opk_close(o);
	} else {
		char buf[OD_META];
		struct entry *e=new_entry(source,"",0);
		if(e) {
			if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<1||st.st_size>OD_META||
			   od_fd_read(&fd,0,buf,(size_t)st.st_size))
				od_fail(e->d.reason,"Desktop cannot be read safely or exceeds metadata bounds");
			else {
				od_parse(buf,(size_t)st.st_size,&e->d);
				if(!e->d.name[0]) {
					const char *base=strrchr(source,'/');
					od_clean(e->d.name,sizeof(e->d.name),base?base+1:source,strlen(base?base+1:source));
				}
				guard_entry(e,NULL);
			}
		}
	}
	if(fd>=0)close(fd);
}
static void scan(void)
{
	unsigned i;
	for(i=0;i<2;++i) {
		int fd=od_open_path(roots[i],O_RDONLY|O_DIRECTORY);
		DIR *dir;struct dirent *ent;unsigned visited=0;
		if(fd<0)continue;
		dir=fdopendir(fd);
		if(!dir){close(fd);continue;}
		while((ent=readdir(dir))) {
			char path[OD_PATH];
			if(++visited>OD_NODES||count==OD_CATALOG||od_expired())break;
			if(!od_component(ent->d_name)||(!ends(ent->d_name,".desktop")&&!ends(ent->d_name,".opk"))||
			   od_join(path,roots[i],ent->d_name))continue;
			scan_source(path,ends(path,".opk"));
		}
		closedir(dir);
	}
}
static int cache_usage(int fd,unsigned depth,uint64_t *bytes,unsigned *nodes)
{
	DIR *dir;struct dirent *ent;
	int copy=dup(fd),result=0;
	if(copy<0||!(dir=fdopendir(copy))) {if(copy>=0)close(copy);return -1;}
	while((ent=readdir(dir))) {
		struct stat st;int child;
		if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,".."))continue;
		if(++*nodes>16384||depth>OD_DEPTH+2||od_expired()||
		   !od_component(ent->d_name)||fstatat(fd,ent->d_name,&st,AT_SYMLINK_NOFOLLOW)) {result=-1;break;}
		if(S_ISDIR(st.st_mode)) {
			child=openat(fd,ent->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
			if(child<0||cache_usage(child,depth+1,bytes,nodes)){if(child>=0)close(child);result=-1;break;}
			close(child);
		} else if(!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<0||
		          (uint64_t)st.st_size>OD_CACHE_MAX-*bytes) {result=-1;break;}
		else *bytes+=(uint64_t)st.st_size;
	}
	closedir(dir);return result;
}
static int prepare_cache(struct entry *e,struct od_opk *o,int image,char out[OD_PATH])
{
	char hash[65],after[65],tmp[96],path[OD_PATH],reason[OD_REASON]={0};
	int root=-1,dir=-1,payload=-1,stamp=-1,result=-1,exists=0,locked=0;
	uint64_t bytes=0,written=0;
	unsigned nodes=0;
	struct statvfs space;
	struct stat st;
	if(fstat(image,&st)||od_hash_fd(image,(uint64_t)st.st_size,hash))
		return od_fail(e->d.reason,"OPK content hashing failed or timed out");
	root=od_mkdir_path(cache_root);
	if(root<0)return od_fail(e->d.reason,"OPK cache storage is not writable");
	if(acquire_lock(root,e->d.reason))goto done;
	locked=1;
	if(fstatvfs(root,&space)||space.f_flag&ST_NOEXEC) {
		od_fail(e->d.reason,"OPK cache mount is noexec or unavailable");goto done;
	}
	if(cache_usage(root,0,&bytes,&nodes)) {
		od_fail(e->d.reason,"OPK cache is unsafe or exceeds its 512 MiB budget");goto done;
	}
	dir=openat(root,hash,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
	exists=dir>=0;
	if(exists) {
		char marker[65];
		stamp=openat(dir,"complete",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
		if(stamp<0||fstat(stamp,&st)||!S_ISREG(st.st_mode)||st.st_size!=64||
		   od_fd_read(&stamp,0,marker,64)||memcmp(marker,hash,64)) {
			od_fail(e->d.reason,"Incomplete or modified OPK cache; remove its directory");goto done;
		}
	} else {
		if(errno!=ENOENT) {od_fail(e->d.reason,"Unsafe cache content-hash directory");goto done;}
		if(e->expanded+64>OD_CACHE_MAX-bytes) {
			od_fail(e->d.reason,"512 MiB OPK cache budget exceeded; remove unused cache entries");goto done;
		}
		if(space.f_bavail && e->expanded+1024*1024>
		   (uint64_t)space.f_bavail*space.f_frsize) {
			od_fail(e->d.reason,"Insufficient free space for OPK cache");goto done;
		}
		snprintf(tmp,sizeof(tmp),".tmp-%s-%ld",hash,(long)getpid());
		if(mkdirat(root,tmp,0700)){od_fail(e->d.reason,"Cannot create atomic OPK cache");goto done;}
		dir=openat(root,tmp,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
		if(dir<0||mkdirat(dir,"payload",0700)){od_fail(e->d.reason,"Cannot create OPK payload directory");goto cleanup;}
	}
	payload=openat(dir,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
	if(payload<0||od_opk_extract(o,payload,exists,&written,reason)) {
		od_fail(e->d.reason,reason[0]?reason:"Invalid OPK cache payload");goto cleanup;
	}
	if(exists) {
		bytes=0;nodes=0;
		if(cache_usage(payload,0,&bytes,&nodes)||nodes!=e->nodes||bytes!=e->expanded) {
			od_fail(e->d.reason,"Unexpected files in OPK cache payload");goto done;
		}
	}
	if(fstat(image,&st)||od_hash_fd(image,(uint64_t)st.st_size,after)||strcmp(after,hash)) {
		od_fail(e->d.reason,"OPK changed during extraction or validation");goto cleanup;
	}
	if(!exists) {
		stamp=openat(dir,"complete",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
		if(stamp<0||od_write(stamp,hash,64)||fsync(stamp)||fsync(payload)||fsync(dir)||
		   renameat(root,tmp,root,hash)||fsync(root)) {
			od_fail(e->d.reason,"Atomic OPK cache publication failed");goto cleanup;
		}
	}
	if(od_join(path,cache_root,hash)||od_join(out,path,"payload")) {
		od_fail(e->d.reason,"Cache path exceeds bounds");goto done;
	}
	result=0;goto done;
cleanup:
	if(!exists)od_remove_at(root,tmp,0);
done:
	if(stamp>=0)close(stamp);
	if(payload>=0)close(payload);
	if(dir>=0)close(dir);
	if(locked)release_lock();
	if(root>=0)close(root);
	return result;
}
static int launch(struct entry *e,const char *rom,struct rlimit *saved,int menu)
{
	char executable[OD_PATH],cwd[OD_PATH],home[OD_PATH],*argv[OD_ARGS];
	int fd=-1,dir=-1,image=-1,hfd=-1;
	struct od_opk *o=NULL;
	unsigned i;
	const char *hcfb=getenv("LILSMOKE_HCFB");
	const int m8=hcfb&&!strcmp(hcfb,"1")&&access("/os/libm8-display.so",R_OK)==0;
	const char *pixel=getenv("LILSMOKE_DFB_PIXEL_OPTIONS");
	char pixel_options[64],dfb_options[256];
	strcpy(pixel_options,pixel&&!strcmp(pixel,"depth=24,pixelformat=RGB24")?
		"depth=24,pixelformat=RGB24":"depth=32,pixelformat=ARGB");
	if(e->d.reason[0])goto error;
	if(menu&&e->d.menu_exec[0])strcpy(e->d.exec,e->d.menu_exec);
	if(od_lex(&e->d,rom))goto error;
	if(e->d.needs_rom&&!rom&&!menu) {od_fail(e->d.reason,"This application requires a ROM");goto error;}
	if(rom) {
		struct stat st;
		if(rom[0]!='/'||strlen(rom)>=OD_ARG||stat(rom,&st)||!S_ISREG(st.st_mode)) {
			od_fail(e->d.reason,"ROM must be an existing absolute regular file");goto error;
		}
	}
	if(access("/run/lilsmoke-storage/exporting",F_OK)==0) {
		od_fail(e->d.reason,"Eject LILDRIVE before launching OpenDingux");goto error;
	}
	if(e->builtin)strcpy(executable,"/usr/lib/lilsmoke/od-launch");
	else if(e->opk) {
		char rel[OD_PATH];
		image=od_open_path(e->source,O_RDONLY);
		o=image<0?NULL:od_opk_open(image,e->d.reason);
		if(!o||guard_entry(e,o)||prepare_cache(e,o,image,cwd)||executable_path(e,rel)||
		   od_join(executable,cwd,rel))goto error;
		od_opk_close(o);o=NULL;close(image);image=-1;
		dir=od_open_path(cwd,O_RDONLY|O_DIRECTORY);
		if(dir<0||fchdir(dir)) {od_fail(e->d.reason,"Cannot enter OPK payload");goto error;}
	} else {
		char *slash;
		if(executable_path(e,executable))goto error;
		strcpy(cwd,e->source);slash=strrchr(cwd,'/');*slash=0;
		dir=od_open_path(cwd,O_RDONLY|O_DIRECTORY);
		if(dir<0||fchdir(dir)) {od_fail(e->d.reason,"Cannot enter native application folder");goto error;}
	}
	if(!e->builtin&&native_guard(executable,e->d.reason,&fd))goto error;
	if(e->builtin) {
		if(native_guard("/usr/lib/lilsmoke/od-emulators/pocketsnes",e->d.reason,NULL))goto error;
		/* Only this immutable shipped launcher is allowed to be a script. */
		fd=od_open_path(executable,O_RDONLY);
		if(fd<0){od_fail(e->d.reason,"PocketSNES launcher is missing");goto error;}
	}
	if(od_join(home,home_root,e->id)||(hfd=od_mkdir_path(home))<0) {
		od_fail(e->d.reason,"Persistent per-application HOME is unavailable");goto error;
	}
	close(hfd);hfd=-1;
	if(!e->builtin) {
		clearenv();
		setenv("PATH","/usr/bin:/bin",1);
		setenv("SDL_VIDEODRIVER","directfb",1);
		setenv("SDL_RENDER_DRIVER","software",1);
		/* HC16xx has no ALSA device; source-adapted engines use the native PCM worker. */
		setenv("SDL_AUDIODRIVER",m8?"dummy":"alsa",1);
		setenv("SDL12COMPAT_OPENGL_SCALING","0",1);
		setenv("SDL12COMPAT_ALLOW_THREADED_PUMPS","0",1);
		setenv("SDL12COMPAT_ALLOW_THREADED_DRAWS","0",1);
		setenv("LILSMOKE_DFB_EVDEV_KEYCODES","1",1);
		setenv("DFBARGS","no-hardware,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force",1);
	}
	unsetenv("LD_PRELOAD");unsetenv("LD_LIBRARY_PATH");unsetenv("LD_AUDIT");
	if(m8) {
		/* Restore only the fixed OS-owned adapter, never an inherited/user preload. */
		setenv("LD_PRELOAD","/os/libm8-display.so",1);
		setenv("LILSMOKE_HCFB","1",1);
		setenv("LILSMOKE_M8_AUDIO","1",1);
		setenv("LILSMOKE_M8_NATIVE_VIDEO","1",1);
		setenv("LILSMOKE_AUDIO_OVERRIDE","dummy",1);
		setenv("LILSMOKE_DFB_PIXEL_OPTIONS",pixel_options,1);
		snprintf(dfb_options,sizeof(dfb_options),"no-hardware,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,%s",pixel_options);
		setenv("DFBARGS",dfb_options,1);
	}
	unsetenv("DISPLAY");unsetenv("WAYLAND_DISPLAY");
	setenv("HOME",home,1);setenv("XDG_CONFIG_HOME",home,1);
	setenv("XDG_DATA_HOME",home,1);setenv("XDG_CACHE_HOME",home,1);
	argv[0]=executable;
	for(i=1;i<e->d.argc;++i)argv[i]=e->d.args[i];
	argv[e->d.argc]=NULL;
	if(dir>=0){close(dir);dir=-1;}
	setrlimit(RLIMIT_AS,saved);
	/* Kernel 4.4 has execveat; musl fexecve falls back to /proc if necessary. */
	if(e->builtin)fcntl(fd,F_SETFD,0);
	fexecve(fd,argv,environ);
	od_fail(e->d.reason,errno==EACCES?"Executable cache is noexec or permission denied":"Native exec failed");
error:
	if(fd>=0)close(fd);
	if(dir>=0)close(dir);
	if(image>=0)close(image);
	if(hfd>=0)close(hfd);
	od_opk_close(o);
	fprintf(stderr,"%.191s\n",e->d.reason[0]?e->d.reason:"OpenDingux launch failed");
	return 3;
}
int main(int argc,char **argv)
{
	unsigned i;struct rlimit saved,limit;
	if(argc<2||(!strcmp(argv[1],"scan")?argc!=2:
	   !strcmp(argv[1],"menu")?argc!=3:strcmp(argv[1],"run")||argc<3||argc>4)) {
		fprintf(stderr,"usage: lilsmoke-opendingux scan | menu ID | run ID [ROM]\n");return 2;
	}
#ifdef OD_TEST_PATHS
	{
		const char *s;
		if((s=getenv("OD_TEST_ROOT0")))roots[0]=s;
		if((s=getenv("OD_TEST_ROOT1")))roots[1]=s;
		if((s=getenv("OD_TEST_CACHE")))cache_root=s;
		if((s=getenv("OD_TEST_HOME")))home_root=s;
	}
#endif
	if(getrlimit(RLIMIT_AS,&saved))return 3;
	limit=saved;
	if(limit.rlim_cur==RLIM_INFINITY||limit.rlim_cur>48*1024*1024)limit.rlim_cur=48*1024*1024;
#if !defined(OD_SANITIZED) && !defined(OD_HOSTED_QA)
	if(setrlimit(RLIMIT_AS,&limit))return 3;
#endif
	od_deadline(!strcmp(argv[1],"scan")?4:30);
	scan();
	if(!strcmp(argv[1],"scan")) {
		for(i=0;i<count;++i)printf("%s\t%s\t%s\t%s\t%d\n",catalog[i].id,catalog[i].d.name,
			catalog[i].d.systems,catalog[i].d.reason,catalog[i].d.needs_rom);
		return 0;
	}
	for(i=0;i<count;++i)if(!strcmp(argv[2],catalog[i].id))
		return launch(&catalog[i],argc==4?argv[3]:NULL,&saved,!strcmp(argv[1],"menu"));
	fprintf(stderr,"Unknown application ID or catalog/time limit reached\n");return 3;
}
