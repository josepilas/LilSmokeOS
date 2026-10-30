/* SPDX-License-Identifier: AGPL-3.0-only */
#include "port.h"
#include <sys/stat.h>
#include <unistd.h>

static uint32_t le32(const unsigned char* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static unsigned le16(const unsigned char* p) {return p[0]|((unsigned)p[1]<<8);}
static int regular(const char* path,long cap) {
    struct stat s;return !lstat(path,&s) && S_ISREG(s.st_mode) && s.st_size>=0 && s.st_size<=cap;
}
int ft_validate_data(const char* path,char* error,size_t cap) {
    FILE* f=NULL;unsigned char hdr[32],r[32];char dir[512],name[550];
    const char* why="Invalid or missing WAD"; uint32_t count=0;bool gen=false,tpag=false;
    if(strlen(path)>=sizeof dir || !regular(path,128*1024*1024)) goto fail;
    snprintf(dir,sizeof dir,"%s",path);char* slash=strrchr(dir,'/');
    if(slash) *slash=0;else strcpy(dir,".");
    f=fopen(path,"rb");if(!f) goto fail;
    if(fread(hdr,1,8,f)!=8 || memcmp(hdr,"FORM",4)) goto fail;
    fseek(f,0,SEEK_END);long end=ftell(f);
    if(end<8 || le32(hdr+4)!=(uint32_t)end-8) goto fail;
    long pos=8;
    while(pos<end) {
        if(end-pos<8 || fseek(f,pos,SEEK_SET) || fread(hdr,1,8,f)!=8) goto fail;
        uint32_t n=le32(hdr+4);if(n>(uint32_t)(end-pos-8)) goto fail;
        if(!memcmp(hdr,"GEN8",4)) {
            if(gen || n<128 || fread(r,1,4,f)!=4 || r[1]!=16) {why="Only WAD16 is enabled";goto fail;}
            gen=true;
        }
        if(!memcmp(hdr,"TPAG",4)) {
            if(tpag || n<4 || fread(r,1,4,f)!=4) goto fail;
            count=le32(r);tpag=true;
            if(count>32768 || count>(n-4)/4) goto fail;
        }
        pos+=8+n;
    }
    fclose(f);f=NULL;
    if(!gen || !tpag) goto fail;
    const char* prepared=getenv("SW_ASSET_DIR");
    if(prepared) {if(strlen(prepared)>=sizeof dir)goto fail;strcpy(dir,prepared);}
    why="Missing or invalid matching BYO atlas";
    snprintf(name,sizeof name,"%s/ATLAS.RGBA4444",dir);
    if(!regular(name,256*1024*1024)) goto fail;
    struct stat px;if(stat(name,&px)) goto fail;
    snprintf(name,sizeof name,"%s/ATLAS.BIN",dir);
    if(!regular(name,32+32768*32)) goto fail;
    f=fopen(name,"rb");if(!f) goto fail;
    if(fread(hdr,1,32,f)!=32 || memcmp(hdr,"ATLS",4) || le32(hdr+4)!=1 || le32(hdr+8)!=count) goto fail;
    for(uint32_t i=0;i<count;++i) {
        if(fread(r,1,32,f)!=32) goto fail;
        uint32_t off=le32(r+16);unsigned w=le16(r+12),h=le16(r+14);
        if(w>1024 || h>1024 || (off && (off>(uint32_t)px.st_size || w*h*2>(uint32_t)px.st_size-off))) goto fail;
    }
    if(fgetc(f)!=EOF) goto fail;
    fclose(f);return 0;
fail:
    if(f) fclose(f);snprintf(error,cap,"%s",why);return 1;
}
