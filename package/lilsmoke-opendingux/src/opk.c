#include "od.h"
#include <sqfs/io.h>
#include <sqfs/super.h>
#include <sqfs/dir.h>
#include <sqfs/dir_reader.h>
#include <sqfs/data_reader.h>
#include <sqfs/inode.h>
#include <sqfs/compressor.h>
#include <sqfs/error.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct input {
	sqfs_file_t file;
	int fd;
	uint64_t size;
};
struct od_opk {
	struct input input;
	sqfs_super_t super;
	sqfs_compressor_t *cmp;
	sqfs_data_reader_t *data;
	unsigned nodes;
	uint64_t total;
	uint64_t seen[8192];
};
struct source {struct od_opk *opk; sqfs_inode_generic_t *inode;};
static int input_read(sqfs_file_t *file,sqfs_u64 at,void *p,size_t n)
{
	struct input *in=(struct input *)file;
	if(at>in->size||n>in->size-at)return SQFS_ERROR_IO;
	return od_fd_read(&in->fd,at,p,n)?SQFS_ERROR_IO:0;
}
static sqfs_u64 input_size(const sqfs_file_t *file)
{
	return ((const struct input *)file)->size;
}
static int source_read(void *ctx,uint64_t at,void *p,size_t n)
{
	struct source *s=ctx;
	sqfs_u64 size;
	if(n>32768||sqfs_inode_get_file_size(s->inode,&size)||at>size||n>size-at||od_expired())
		return -1;
	return sqfs_data_reader_read(s->opk->data,s->inode,at,p,(sqfs_u32)n)==(sqfs_s32)n?0:-1;
}
static int make_reader(struct od_opk *opk,sqfs_inode_generic_t *node,struct source *s,struct od_reader *r)
{
	sqfs_u64 size;
	if(sqfs_inode_get_file_size(node,&size)||size>OD_FILE_MAX)return -1;
	s->opk=opk;s->inode=node;r->ctx=s;r->size=size;r->read=source_read;
	return 0;
}
struct od_opk *od_opk_open(int fd,char *reason)
{
	struct stat st;
	struct od_opk *o=calloc(1,sizeof(*o));
	sqfs_compressor_config_t cfg;
	if(!o){od_fail(reason,"OPK reader allocation failed");return NULL;}
	o->input.fd=fd;o->input.file.read_at=input_read;o->input.file.get_size=input_size;
	if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<96||(uint64_t)st.st_size>OD_IMAGE_MAX)
		od_fail(reason,"OPK is not a regular bounded SquashFS image");
	else {
		o->input.size=(uint64_t)st.st_size;
		if(sqfs_super_read(&o->super,&o->input.file))
			od_fail(reason,"Invalid or unsupported SquashFS v4 superblock");
		else if(o->super.bytes_used>o->input.size||o->super.block_size>131072||
			o->super.inode_count>OD_NODES||o->super.fragment_entry_count>OD_NODES||
			o->super.id_count>OD_NODES)
			od_fail(reason,"SquashFS exceeds block, inode, fragment or image bounds");
		else if(o->super.compression_id!=SQFS_COMP_GZIP&&o->super.compression_id!=SQFS_COMP_XZ)
			od_fail(reason,"Only gzip and bounded XZ OPK compression is supported");
		else if(sqfs_compressor_config_init(&cfg,o->super.compression_id,o->super.block_size,
				SQFS_COMP_FLAG_UNCOMPRESS)||sqfs_compressor_create(&cfg,&o->cmp))
			od_fail(reason,"SquashFS decompressor unavailable");
		else if((o->super.flags&SQFS_FLAG_COMPRESSOR_OPTIONS)&&
			o->cmp->read_options(o->cmp,&o->input.file))
			od_fail(reason,"Invalid SquashFS compressor options");
		else {
			o->data=sqfs_data_reader_create(&o->input.file,o->super.block_size,o->cmp,0);
			if(!o->data||sqfs_data_reader_load_fragment_table(o->data,&o->super))
				od_fail(reason,"Invalid SquashFS fragment table");
		}
	}
	if(reason[0]){od_opk_close(o);return NULL;}
	return o;
}
void od_opk_close(struct od_opk *o)
{
	if(!o)return;
	sqfs_destroy(o->data);sqfs_destroy(o->cmp);free(o);
}
static int remember(struct od_opk *o,const char *path)
{
	uint64_t hash=1469598103934665603ULL;
	unsigned slot,i;
	for(;*path;++path) {
		unsigned char c=(unsigned char)*path;
		if(c>='A'&&c<='Z')c+=32;
		hash=(hash^c)*1099511628211ULL;
	}
	if(!hash)hash=1;
	slot=(unsigned)(hash&8191);
	for(i=0;i<8192;++i,slot=(slot+1)&8191) {
		if(o->seen[slot]==hash)return -1;
		if(!o->seen[slot]){o->seen[slot]=hash;return 0;}
	}
	return -1;
}
static int walk(struct od_opk *o,sqfs_inode_generic_t *dir,const char *prefix,
		unsigned depth,od_entry_cb cb,void *ctx,char *reason)
{
	sqfs_dir_reader_t *rd;
	sqfs_dir_entry_t *ent=NULL;
	sqfs_inode_generic_t *node=NULL;
	char path[OD_PATH],name[121];
	int r,result=0;
	if(depth>OD_DEPTH||od_expired())return od_fail(reason,"OPK depth or time limit exceeded");
	rd=sqfs_dir_reader_create(&o->super,o->cmp,&o->input.file,0);
	if(!rd||sqfs_dir_reader_open_dir(rd,dir,0)) {
		sqfs_destroy(rd);return od_fail(reason,"Invalid SquashFS directory");
	}
	while((r=sqfs_dir_reader_read(rd,&ent))==0) {
		struct source s;struct od_reader reader;
		unsigned mode=0,t;
		size_t n=(size_t)ent->size+1;
		if(n>=sizeof(name)||memchr(ent->name,0,n)) {
			result=od_fail(reason,"Unsafe or oversized SquashFS filename");break;
		}
		memcpy(name,ent->name,n);name[n]=0;
		if(!od_component(name)||od_join(path,prefix,name)||remember(o,path)||
		   ++o->nodes>OD_NODES||od_expired()) {
			result=od_fail(reason,"Unsafe path, FAT case collision, node or time limit");break;
		}
		if(sqfs_dir_reader_get_inode(rd,&node)) {
			result=od_fail(reason,"Invalid SquashFS inode");break;
		}
		t=node->base.type;
		if(t==SQFS_INODE_DIR||t==SQFS_INODE_EXT_DIR)mode=S_IFDIR|0700;
		else if(t==SQFS_INODE_FILE||t==SQFS_INODE_EXT_FILE) {
			if(make_reader(o,node,&s,&reader)||reader.size>OD_CACHE_MAX-o->total) {
				result=od_fail(reason,"File or expanded OPK budget exceeded");break;
			}
			o->total+=reader.size;mode=S_IFREG|(node->base.mode&0111?0700:0600);
		} else {
			result=od_fail(reason,"OPK links, devices, sockets and FIFOs are rejected");break;
		}
		if(cb&&cb(o,path,S_ISREG(mode)?&reader:NULL,mode,ctx)) {result=-1;break;}
		if(S_ISDIR(mode)&&walk(o,node,path,depth+1,cb,ctx,reason)){result=-1;break;}
		sqfs_free(node);node=NULL;sqfs_free(ent);ent=NULL;
	}
	if(r<0&&!result)result=od_fail(reason,"Corrupt SquashFS directory listing");
	sqfs_free(node);sqfs_free(ent);sqfs_destroy(rd);return result;
}
int od_opk_walk(struct od_opk *o,od_entry_cb cb,void *ctx,char *reason)
{
	sqfs_dir_reader_t *rd=sqfs_dir_reader_create(&o->super,o->cmp,&o->input.file,0);
	sqfs_inode_generic_t *root=NULL;
	int r;
	o->nodes=0;o->total=0;memset(o->seen,0,sizeof(o->seen));
	if(!rd||sqfs_dir_reader_get_root_inode(rd,&root)||
	   (root->base.type!=SQFS_INODE_DIR&&root->base.type!=SQFS_INODE_EXT_DIR)) {
		sqfs_destroy(rd);sqfs_free(root);return od_fail(reason,"Invalid SquashFS root");
	}
	sqfs_destroy(rd);r=walk(o,root,"",0,cb,ctx,reason);sqfs_free(root);return r;
}
int od_opk_reader(struct od_opk *o,const char *path,struct od_reader *r,void **token,char *reason)
{
	sqfs_dir_reader_t *rd;
	struct source *s;
	*token=NULL;
	if(!od_relative(path))return od_fail(reason,"Executable path is unsafe");
	s=calloc(1,sizeof(*s));
	if(!s)return od_fail(reason,"Inode allocation failed");
	rd=sqfs_dir_reader_create(&o->super,o->cmp,&o->input.file,0);
	if(!rd||sqfs_dir_reader_find_by_path(rd,NULL,path,&s->inode)||make_reader(o,s->inode,s,r)) {
		sqfs_destroy(rd);od_opk_reader_free(s);return od_fail(reason,"OPK executable is missing or not a bounded regular file");
	}
	sqfs_destroy(rd);*token=s;return 0;
}
void od_opk_reader_free(void *token)
{
	struct source *s=token;
	if(s){sqfs_free(s->inode);free(s);}
}
struct extract {int root,verify;uint64_t total;char *reason;};
static int extract_cb(struct od_opk *o,const char *path,const struct od_reader *r,unsigned mode,void *ctx)
{
	struct extract *x=ctx;
	char temp[OD_PATH],*part,*next;
	int dir=dup(x->root),fd=-1,result=-1;
	unsigned char source[32768],cached[32768];
	uint64_t at=0;
	struct stat st;
	(void)o;
	if(dir<0)return od_fail(x->reason,"Extraction directory unavailable");
	strcpy(temp,path);part=temp;
	while((next=strchr(part,'/'))) {
		*next=0;
		fd=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
		close(dir);dir=fd;fd=-1;part=next+1;
		if(dir<0)goto done;
	}
	if(S_ISDIR(mode)) {
		if(!x->verify&&mkdirat(dir,part,0700))goto done;
		fd=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
		result=fd<0?-1:0;goto done;
	}
	fd=openat(dir,part,(x->verify?O_RDONLY:O_WRONLY|O_CREAT|O_EXCL)|O_NOFOLLOW|O_CLOEXEC,0700);
	if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||
	   (x->verify&&(uint64_t)st.st_size!=r->size))goto done;
	while(at<r->size) {
		size_t n=sizeof(source);if(n>r->size-at)n=(size_t)(r->size-at);
		if(r->read(r->ctx,at,source,n))goto done;
		if(x->verify) {
			if(od_fd_read(&fd,at,cached,n)||memcmp(source,cached,n))goto done;
		} else if(od_write(fd,source,n))goto done;
		at+=n;
	}
	if(!x->verify&&fsync(fd))goto done;
	x->total+=r->size;result=0;
done:
	if(fd>=0)close(fd);
	if(dir>=0)close(dir);
	if(result)return od_fail(x->reason,x->verify?"Cache content changed or unsafe; remove this cache entry":"Streaming extraction failed");
	return 0;
}
int od_opk_extract(struct od_opk *o,int root,int verify,uint64_t *bytes,char *reason)
{
	struct extract x={root,verify,0,reason};
	int r=od_opk_walk(o,extract_cb,&x,reason);
	*bytes=x.total;return r;
}
