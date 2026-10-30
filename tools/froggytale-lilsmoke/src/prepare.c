/* SPDX-License-Identifier: AGPL-3.0-only
 * Native counterpart of pinned FroggyTale tools/pack_assets.py.
 * Format/dedup/page-at-a-time layout is retained; cap filtering uses integer box averages.
 */
#include "port.h"
#include "sw_renderer.h"
#include "stb_image.h"
#include "sha1.h"
#include "prep_lock.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

extern bool ft_input_cancel_requested(void);
extern void ft_text(Renderer*,const char*,int,int,int,uint32_t);
static Renderer* progress_renderer;
static unsigned last_percent=101;
static uint64_t progress_bytes,progress_total;
static uint32_t atlas_bytes;
static uint32_t rd32(const unsigned char* p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static unsigned rd16(const unsigned char* p) {return p[0]|((unsigned)p[1]<<8);}
static void wr32(unsigned char* p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
static void wr16(unsigned char* p,unsigned v) {p[0]=v;p[1]=v>>8;}
static bool range(size_t size,uint32_t off,uint32_t bytes) {return off<=size && bytes<=size-off;}
static bool directory(const char* p) {
    struct stat s;if(mkdir(p,0700)<0 && errno!=EEXIST)return false;
    return !lstat(p,&s) && S_ISDIR(s.st_mode);
}
static bool progress(const char* stage,unsigned percent) {
    bool changed=percent!=last_percent;
    if(changed) {printf("UNDERTALE_PREP progress=%u stage=%s input_bytes=%llu total_bytes=%llu atlas_bytes=%u\n",
        percent,stage,(unsigned long long)progress_bytes,(unsigned long long)progress_total,atlas_bytes);last_percent=percent;}
    if(progress_renderer && changed) {
        Renderer* r=progress_renderer;
        r->vtable->beginFrame(r,320,240,320,240);r->vtable->beginGUI(r,320,240,0,0,320,240,-1);
        r->vtable->clearScreen(r,0x201810,1);
        ft_text(r,"Undertale-LilSmoke",12,20,2,0xffffff);
        char status[200];snprintf(status,sizeof status,"%s\n%u%%\nOriginal: %.1f / %.1f MB\nAtlas: %.1f MB",stage,percent,
            progress_bytes/1000000.0,progress_total/1000000.0,atlas_bytes/1000000.0);
        ft_text(r,status,12,65,1,0xffffff);
        r->vtable->drawRectangle(r,12,135,308,147,0x404040,1,false);
        r->vtable->drawRectangle(r,12,135,12+296*percent/100,147,0x70d050,1,false);
        ft_text(r,"Preparing original data\nB / Escape: cancel\nOriginal file is never changed",12,175,1,0xffffff);
        r->vtable->endGUI(r);SwRenderer_present();
    }
    const char* cancel=getenv("UNDERTALE_PREP_CANCEL_AT");
    if(cancel && percent>=strtoul(cancel,NULL,10)) {errno=ECANCELED;return false;}
    if(ft_input_poll() || ft_input_cancel_requested()) {errno=ECANCELED;return false;}
    return true;
}
static bool file_hash(const char* path,char hash[41],bool show) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return false;
    struct stat st;if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || st.st_size>256*1024*1024) {close(fd);return false;}
    SHA1_CTX ctx;SHA1Init(&ctx);unsigned char buffer[65536],digest[20];ssize_t n;uint64_t done=0;
    while((n=read(fd,buffer,sizeof buffer))>0) {
        SHA1Update(&ctx,buffer,n);done+=n;
        if(show) {progress_bytes=done;progress_total=st.st_size;}
        if(show && !progress("Checking original",(unsigned)(done*15/(st.st_size?st.st_size:1)))) {close(fd);return false;}
        if(!show && (ft_input_poll() || ft_input_cancel_requested())) {errno=ECANCELED;close(fd);return false;}
    }
    close(fd);if(n<0)return false;SHA1Final(digest,&ctx);
    for(unsigned i=0;i<20;++i)snprintf(hash+i*2,3,"%02x",digest[i]);return true;
}
static bool durable(FILE* f) {return fflush(f)==0 && fsync(fileno(f))==0;}
static bool metadata(const char* path,char* out,size_t cap) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return false;
    struct stat st,after;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0) {close(fd);return false;}
    SHA1_CTX ctx;SHA1Init(&ctx);unsigned char data[65536],digest[20];char hex[41];
    size_t n=st.st_size<(off_t)sizeof data?(size_t)st.st_size:sizeof data;
    bool ok=pread(fd,data,n,0)==(ssize_t)n;if(ok)SHA1Update(&ctx,data,n);
    if(ok && st.st_size>(off_t)n) {
        ok=pread(fd,data,n,st.st_size-n)==(ssize_t)n;if(ok)SHA1Update(&ctx,data,n);
    }
    /* Device/inode are useful only for this live read, never a FAT cold-boot key. */
    ok=ok&&!fstat(fd,&after)&&st.st_dev==after.st_dev&&st.st_ino==after.st_ino&&
        st.st_size==after.st_size&&st.st_mtim.tv_sec==after.st_mtim.tv_sec&&st.st_mtim.tv_nsec==after.st_mtim.tv_nsec;
    close(fd);if(!ok)return false;SHA1Final(digest,&ctx);
    for(unsigned i=0;i<20;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
    snprintf(out,cap,"%llu %lld %ld %s\n",(unsigned long long)st.st_size,
        (long long)st.st_mtim.tv_sec,st.st_mtim.tv_nsec,hex);return true;
}
static bool seal_active(const char* cache,const char* source,const char* dir,const char* hash) {
    char tmp[700],target[700],path[700],info[256];
    if(strchr(source,'\n')||strlen(source)>600)return false;
    snprintf(tmp,sizeof tmp,"%s/.active-%ld",cache,(long)getpid());
    snprintf(target,sizeof target,"%s/active.txt",cache);
    FILE* f=fopen(tmp,"wx");if(!f)return false;
    bool ok=fprintf(f,"Undertale-LilSmoke sealed-metadata-v2\n%s\n%s\n",hash,source)>0;
    const char* names[]={NULL,"ATLAS.BIN","ATLAS.RGBA4444","complete.txt"};
    for(unsigned i=0;i<4&&ok;++i) {
        if(i)snprintf(path,sizeof path,"%s/%s",dir,names[i]);else snprintf(path,sizeof path,"%s",source);
        ok=metadata(path,info,sizeof info)&&fputs(info,f)>=0;
    }
    ok=ok&&durable(f);if(fclose(f))ok=false;
    if(ok&&rename(tmp,target)==0)return true;unlink(tmp);return false;
}
static bool quick_cache(const char* cache,const char* source,char* dir,size_t cap,char hash[41]) {
    char path[700],line[700],expected[256],actual[256];
    snprintf(path,sizeof path,"%s/active.txt",cache);FILE* f=fopen(path,"r");if(!f)return false;
    bool ok=fgets(line,sizeof line,f)&&!strcmp(line,"Undertale-LilSmoke sealed-metadata-v2\n");
    if(!ok||!fgets(line,sizeof line,f)||strlen(line)!=41) {fclose(f);return false;}
    memcpy(hash,line,40);hash[40]=0;
    for(unsigned i=0;i<40;++i)if(!((hash[i]>='0'&&hash[i]<='9')||(hash[i]>='a'&&hash[i]<='f')))ok=false;
    if(!fgets(line,sizeof line,f))ok=false;
    else {line[strcspn(line,"\n")]=0;if(strcmp(line,source))ok=false;}
    if(snprintf(dir,cap,"%s/%s-v1",cache,hash)>=(int)cap)ok=false;
    const char* names[]={NULL,"ATLAS.BIN","ATLAS.RGBA4444","complete.txt"};
    for(unsigned i=0;i<4&&ok;++i) {
        if(i)snprintf(path,sizeof path,"%s/%s",dir,names[i]);else snprintf(path,sizeof path,"%s",source);
        ok=fgets(expected,sizeof expected,f)&&metadata(path,actual,sizeof actual)&&!strcmp(expected,actual);
    }
    if(fgetc(f)!=EOF)ok=false;fclose(f);if(!ok)return false;
    setenv("SW_ASSET_DIR",dir,1);char error[160];
    if(ft_validate_data(source,error,sizeof error))return false;
    struct stat st;if(!stat(source,&st))progress_bytes=progress_total=st.st_size;
    snprintf(path,sizeof path,"%s/ATLAS.RGBA4444",dir);if(!stat(path,&st))atlas_bytes=st.st_size;
    return true;
}
static bool valid_cache(const char* dir,const char* source,char hash[41]) {
    char path[650],line[160],a[41],b[41],wa[41],wb[41];
    snprintf(path,sizeof path,"%s/complete.txt",dir);FILE* f=fopen(path,"r");if(!f)return false;
    bool ok=fgets(line,sizeof line,f) && !strcmp(line,"Undertale-LilSmoke atlas-v1-box320\n") &&
        fgets(line,sizeof line,f) && !strncmp(line,hash,40) && fscanf(f,"%40s %40s",wa,wb)==2;
    fclose(f);if(!ok)return false;
    snprintf(path,sizeof path,"%s/ATLAS.BIN",dir);if(!file_hash(path,a,false))return false;
    snprintf(path,sizeof path,"%s/ATLAS.RGBA4444",dir);if(!file_hash(path,b,false))return false;
    if(strcmp(a,wa)||strcmp(b,wb))return false;
    setenv("SW_ASSET_DIR",dir,1);char error[160];return !ft_validate_data(source,error,sizeof error);
}

int ft_prepare(const char* source,const char* cache,bool headless,char* result,size_t result_size) {
    int ret=1,fd=-1;FtPrepLock lock={.fd=-1};unsigned char* wad=MAP_FAILED;size_t size=0;
    unsigned char* index=NULL;unsigned char* rgba=NULL;FILE *pixels=NULL,*idx=NULL;
    char source_hash[41],path[700],stage[600]={0},dest[600]={0};bool made_stage=false;
    const char* failure="Preparation failed";last_percent=101;progress_bytes=progress_total=atlas_bytes=0;
    if(!ft_platform_open(headless))return 69;
    if(!headless) progress_renderer=SWRenderer_create();
    if(!directory(cache)) {failure="Cache folder unavailable";goto done;}
    if(ft_prep_lock(&lock,cache)) {failure="Preparation cache is busy or unavailable";goto done;}
    if(!getenv("UNDERTALE_VERIFY_CACHE")&&quick_cache(cache,source,dest,sizeof dest,source_hash)) {
        if(!progress("Cache ready",100))goto done;
        snprintf(result,result_size,"%s",dest);printf("UNDERTALE_PREP cache=hit validation=sealed-metadata source_sha1=%s\n",source_hash);ret=0;goto done;
    }
    if(!file_hash(source,source_hash,true)) {failure="Original missing, unreadable or cancelled";goto done;}
    if(snprintf(dest,sizeof dest,"%s/%s-v1",cache,source_hash)>=(int)sizeof dest)goto done;
    if(valid_cache(dest,source,source_hash)) {
        if(!seal_active(cache,source,dest,source_hash)) {failure="Cannot seal validated cache";goto done;}
        char px[700];struct stat ps;snprintf(px,sizeof px,"%s/ATLAS.RGBA4444",dest);if(!stat(px,&ps))atlas_bytes=ps.st_size;
        if(!progress("Cache ready",100))goto done;
        snprintf(result,result_size,"%s",dest);printf("UNDERTALE_PREP cache=hit source_sha1=%s\n",source_hash);ret=0;goto done;
    }
    /* Never replace a completed generation in place. Corruption gets a fresh directory. */
    struct stat exists;
    if(!lstat(dest,&exists)) {
        if(!S_ISDIR(exists.st_mode)) {failure="Cache generation is not a directory";goto done;}
        snprintf(path,sizeof path,"%s.invalid-%ld",dest,(long)getpid());
        if(rename(dest,path)) {failure="Cannot preserve invalid cache";goto done;}
    }
    if(snprintf(stage,sizeof stage,"%s/.stage-%s-%ld-%llu",cache,lock.boot,(long)getpid(),lock.start)>=(int)sizeof stage) {
        failure="Cache path is too long";goto done;
    }
    if(mkdir(stage,0700)) {failure="Staging folder already exists or is unwritable";goto done;}made_stage=true;
    fd=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
    if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<144 || st.st_size>128*1024*1024)goto done;
    size=(size_t)st.st_size;wad=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);if(wad==MAP_FAILED)goto done;
    if(memcmp(wad,"FORM",4)||rd32(wad+4)!=size-8) {failure="Invalid WAD FORM";goto done;}
    uint32_t tpag=0,txtr=0,txtr_end=0;bool gen=false;
    for(uint32_t pos=8;pos<size;) {
        if(!range(size,pos,8))goto done;uint32_t bytes=rd32(wad+pos+4);
        if(!range(size,pos+8,bytes))goto done;
        if(!memcmp(wad+pos,"GEN8",4)) {if(gen||bytes<128||wad[pos+9]!=16) {failure="Only WAD16 is supported";goto done;}gen=true;}
        if(!memcmp(wad+pos,"TPAG",4)) {if(tpag)goto done;tpag=pos+8;}
        if(!memcmp(wad+pos,"TXTR",4)) {if(txtr)goto done;txtr=pos+8;txtr_end=pos+8+bytes;}
        pos+=8+bytes;
    }
    if(!gen||!tpag||!txtr||!range(size,tpag,4)||!range(size,txtr,4)) {failure="WAD16 requires TPAG and TXTR";goto done;}
    uint32_t count=rd32(wad+tpag),pages=rd32(wad+txtr);
    if(!count||count>32768||!pages||pages>256||!range(size,tpag+4,count*4)||!range(size,txtr+4,pages*4))goto done;
    uint32_t stride=count>1?rd32(wad+tpag+8)-rd32(wad+tpag+4):22;
    if(stride!=22&&stride!=26) {failure="Unsupported TPAG stride";goto done;}
    for(uint32_t i=0;i<count;++i) {
        uint32_t item=rd32(wad+tpag+4+i*4);
        if(!range(size,item,stride)||rd16(wad+item+(stride==22?20:24))>=pages) {failure="Invalid TPAG page reference";goto done;}
    }
    index=calloc(count,32);if(!index)goto done;
    struct statvfs disk;if(statvfs(cache,&disk)||disk.f_bavail*(uint64_t)disk.f_frsize<128*1024*1024) {failure="At least 128 MiB cache space required";goto done;}
    snprintf(path,sizeof path,"%s/ATLAS.RGBA4444",stage);pixels=fopen(path,"wb");if(!pixels)goto done;
    /* Offset zero is the upstream empty-record sentinel. Reserve a two-byte prefix. */
    if(fwrite("\0\0",1,2,pixels)!=2)goto done;
    uint32_t offset=2,unique=0;
    for(uint32_t page=0;page<pages;++page) {
        if(!progress("Decoding textures",15+page*70/pages))goto done;
        uint32_t ptr=rd32(wad+txtr+4+page*4);if(!range(size,ptr,8))goto done;
        uint32_t start=rd32(wad+ptr+4),end=txtr_end;
        if(page+1<pages) {uint32_t next=rd32(wad+txtr+8+page*4);if(!range(size,next,8))goto done;end=rd32(wad+next+4);}
        if(end<start||!range(size,start,end-start)||end-start>32*1024*1024)goto done;
        int width,height,channels;
        if(!stbi_info_from_memory(wad+start,end-start,&width,&height,&channels)||width<1||height<1||width>2048||height>2048) {failure="Unsupported texture PNG dimensions";goto done;}
        rgba=stbi_load_from_memory(wad+start,end-start,&width,&height,&channels,4);
        if(!rgba) {failure="Texture PNG decode failed";goto done;}
        for(uint32_t i=0;i<count;++i) {
            uint32_t item=rd32(wad+tpag+4+i*4);if(!range(size,item,stride))goto done;
            const unsigned char* p=wad+item;unsigned pageid=rd16(p+(stride==22?20:24));if(pageid!=page)continue;
            unsigned x=rd16(p),y=rd16(p+2),sw=rd16(p+4),sh=rd16(p+6);unsigned char* rec=index+i*32;
            if(!sw||!sh||x>=(unsigned)width||y>=(unsigned)height)continue;
            uint32_t duplicate=i;
            for(uint32_t j=0;j<i;++j) {
                uint32_t other=rd32(wad+tpag+4+j*4);if(!range(size,other,stride))goto done;
                if(!memcmp(wad+other,p,8)&&rd16(wad+other+(stride==22?20:24))==page) {duplicate=j;break;}
            }
            unsigned cropw=sw<(unsigned)width-x?sw:(unsigned)width-x,croph=sh<(unsigned)height-y?sh:(unsigned)height-y;
            unsigned max=cropw>croph?cropw:croph;
            unsigned w=max>320?(cropw*320+max/2)/max:cropw,h=max>320?(croph*320+max/2)/max:croph;
            if(!w)w=1;if(!h)h=1;
            if(duplicate<i) {
                memcpy(rec+12,index+duplicate*32+12,8);
            } else {
                wr16(rec+12,w);wr16(rec+14,h);wr32(rec+16,offset);
                for(unsigned dy=0;dy<h;++dy) {
                    if((dy&31)==0&&!progress("Packing atlas",15+page*70/pages))goto done;
                    unsigned sy0=dy*croph/h,sy1=(dy+1)*croph/h;if(sy1<=sy0)sy1=sy0+1;
                    for(unsigned dx=0;dx<w;++dx) {
                        unsigned sx0=dx*cropw/w,sx1=(dx+1)*cropw/w;if(sx1<=sx0)sx1=sx0+1;
                        uint32_t sum[4]={0};unsigned samples=0;
                        for(unsigned sy=sy0;sy<sy1;++sy)for(unsigned sx=sx0;sx<sx1;++sx) {
                            const unsigned char* v=rgba+((y+sy)*width+x+sx)*4;
                            for(unsigned c=0;c<4;++c)sum[c]+=v[c];++samples;
                        }
                        unsigned char out[2]={((sum[0]/samples)&0xf0)|((sum[2]/samples)>>4),
                            ((sum[1]/samples)&0xf0)|((sum[3]/samples)>>4)};
                        if(fwrite(out,1,2,pixels)!=2)goto done;
                    }
                }
                offset+=w*h*2;atlas_bytes=offset;++unique;
            }
            wr16(rec,sw);wr16(rec+2,sh);wr16(rec+4,rd16(p+12));wr16(rec+6,rd16(p+14));
            wr16(rec+8,rd16(p+8));wr16(rec+10,rd16(p+10));wr16(rec+20,rd16(p+16));wr16(rec+22,rd16(p+18));wr16(rec+24,page);
        }
        stbi_image_free(rgba);rgba=NULL;
    }
    if(!durable(pixels))goto done;if(fclose(pixels)) {pixels=NULL;goto done;}pixels=NULL;
    snprintf(path,sizeof path,"%s/ATLAS.BIN",stage);idx=fopen(path,"wb");if(!idx)goto done;
    unsigned char hdr[32]={0};memcpy(hdr,"ATLS",4);wr32(hdr+4,1);wr32(hdr+8,count);wr32(hdr+12,pages);wr32(hdr+16,320);wr32(hdr+20,unique);
    if(fwrite(hdr,1,32,idx)!=32||fwrite(index,32,count,idx)!=count||!durable(idx))goto done;
    fclose(idx);idx=NULL;
    /* Reject a changed original before making any cache generation reusable. */
    char recheck[41],ah[41],ph[41];if(!progress("Validating cache",90)||!file_hash(source,recheck,false)||strcmp(recheck,source_hash)) {failure="Original changed during preparation";goto done;}
    snprintf(path,sizeof path,"%s/ATLAS.BIN",stage);if(!file_hash(path,ah,false))goto done;
    snprintf(path,sizeof path,"%s/ATLAS.RGBA4444",stage);if(!file_hash(path,ph,false))goto done;
    snprintf(path,sizeof path,"%s/complete.txt",stage);idx=fopen(path,"wx");if(!idx)goto done;
    if(fprintf(idx,"Undertale-LilSmoke atlas-v1-box320\n%s\n%s %s\n",source_hash,ah,ph)<0||!durable(idx))goto done;
    fclose(idx);idx=NULL;
    if(!valid_cache(stage,source,source_hash)) {failure="Atlas validation failed";goto done;}
    if(getenv("UNDERTALE_PREP_FAIL_BEFORE_COMMIT")) {failure="Injected pre-commit failure";goto done;}
    if(rename(stage,dest)) {failure="Cannot commit prepared cache";goto done;}made_stage=false;
    if(!seal_active(cache,source,dest,source_hash)) {failure="Cannot seal prepared cache";goto done;}
    setenv("SW_ASSET_DIR",dest,1);snprintf(result,result_size,"%s",dest);
    printf("UNDERTALE_PREP cache=created source_sha1=%s records=%u pages=%u pixel_bytes=%u\n",source_hash,count,pages,offset);
    progress("Ready",100);ret=0;
done:
    if(ret && errno==ECANCELED)ret=130;
    if(ret)fprintf(stderr,"UNDERTALE_PREP error=%s errno=%d\n",failure,errno);
    if(pixels)fclose(pixels);if(idx)fclose(idx);if(rgba)stbi_image_free(rgba);free(index);
    if(wad!=MAP_FAILED)munmap(wad,size);if(fd>=0)close(fd);
    if(made_stage) {
        const char* files[]={"ATLAS.BIN","ATLAS.RGBA4444","complete.txt"};
        for(unsigned i=0;i<3;++i) {snprintf(path,sizeof path,"%s/%s",stage,files[i]);unlink(path);}rmdir(stage);
    }
    ft_prep_unlock(&lock);
    if(ret && ret!=130 && progress_renderer) {
        last_percent=101;progress(failure,0);
        while(!ft_input_poll()&&!ft_input_cancel_requested())SDL_Delay(20);
    }
    if(progress_renderer) {progress_renderer->vtable->destroy(progress_renderer);progress_renderer=NULL;}
    ft_platform_close();return ret;
}
