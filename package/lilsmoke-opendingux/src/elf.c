#include "od.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0]|p[1]<<8); }
static uint32_t u32(const unsigned char *p)
{
	return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int read_bytes(const struct od_reader *r,uint64_t at,void *p,size_t n)
{
	return at>r->size||n>r->size-at?-1:r->read(r->ctx,at,p,n);
}
static int allowed_dep(const char *name)
{
	static const char *const list[]={
		"libc.so","libgcc_s.so.1","libstdc++.so.6","libm.so","libpthread.so","libdl.so","librt.so",
		"libSDL-1.2.so.0","libSDL2-2.0.so.0","libz.so.1","libpng16.so.16",
		"libasound.so.2","libfreetype.so.6",NULL
	};
	unsigned i;
	for(i=0;list[i];++i)if(!strcmp(name,list[i]))return 1;
	return 0;
}
int od_elf(const struct od_reader *r,char *reason,int executable)
{
	unsigned char h[52],ph[32][32],dyn[8],buf[160];
	uint32_t flags,phoff,doff=0,dsize=0,straddr=0,strsize=0,stroff=0,need[32];
	uint16_t count,type;
	unsigned i,j,nneed=0,interp=0,dynamic=0,loads=0,terminated=0;
	if(read_bytes(r,0,h,sizeof(h))||memcmp(h,"\177ELF",4))
		return od_fail(reason,"Executable is not ELF; scripts and legacy wrappers are unsupported");
	if(h[4]!=1||h[5]!=1||h[6]!=1||u16(h+18)!=8||u32(h+20)!=1)
		return od_fail(reason,"Requires little-endian ELF32 MIPS");
	type=u16(h+16);flags=u32(h+36);
	if((type!=2&&type!=3)||u16(h+40)!=52||u16(h+42)!=32)
		return od_fail(reason,"Invalid ELF type or header");
	if((flags&0x20)||((flags&0xf000)!=0&&(flags&0xf000)!=0x1000)||
	   (flags&0x0f000000)||(flags&0x600)||
	   ((flags&0xf0000000)!=0x50000000&&(flags&0xf0000000)!=0x70000000))
		return od_fail(reason,"Requires MIPS32/R2 O32, legacy FP ABI (no N32/MIPS64/NaN2008/ASE)");
	phoff=u32(h+28);count=u16(h+44);
	if(!count||count>32||read_bytes(r,phoff,ph,(size_t)count*32))
		return od_fail(reason,"Invalid ELF program header table");
	for(i=0;i<count;++i) {
		uint32_t t=u32(ph[i]),at=u32(ph[i]+4),size=u32(ph[i]+16);
		if(at>r->size||size>r->size-at)return od_fail(reason,"ELF segment exceeds file");
		if(t==1) {
			if(size>u32(ph[i]+20))return od_fail(reason,"Invalid ELF load segment");
			++loads;
		} else if(t==3) {
			if(++interp>1||size<2||size>sizeof(buf)||read_bytes(r,at,buf,size)||
			   buf[size-1]||memchr(buf,0,size-1)||
			   strcmp((char *)buf,"/lib/ld-musl-mipsel.so.1"))
				return od_fail(reason,"Legacy uClibc/glibc interpreter is incompatible; rebuild for musl");
		} else if(t==2) {
			if(++dynamic>1||size>8192||size%8)return od_fail(reason,"Invalid ELF dynamic table");
			doff=at;dsize=size;
		} else if(t==0x70000003) {
			if(size!=24||read_bytes(r,at,buf,24)||u16(buf)!=0||
			   buf[2]!=32||buf[3]>2||buf[4]>1||buf[5]>1||buf[6]!=0||
			   (buf[7]!=0&&buf[7]!=1&&buf[7]!=3&&buf[7]!=5)||
			   u32(buf+8)||(u32(buf+12)&~3U))
				return od_fail(reason,"Unsupported MIPS ABI extensions or FP register ABI");
		}
	}
	if(!loads)return od_fail(reason,"ELF has no load segment");
	if(executable && ((dynamic&&!interp)||(!dynamic&&interp)||(!dynamic&&type!=2)))
		return od_fail(reason,"Dynamic executable needs the native musl loader; static must be ET_EXEC");
	for(i=0;i<dsize;i+=8) {
		uint32_t tag,val;
		if(read_bytes(r,doff+i,dyn,8))return od_fail(reason,"Truncated ELF dynamic table");
		tag=u32(dyn);val=u32(dyn+4);
		if(!tag){terminated=1;break;}
		if(tag==5)straddr=val;
		else if(tag==10)strsize=val;
		else if(tag==1) {
			if(nneed==32)return od_fail(reason,"Too many ELF dependencies");
			need[nneed++]=val;
		} else if(tag==15||tag==29||tag==0x7ffffffd||tag==0x7fffffff)
			return od_fail(reason,"ELF custom loader paths/audit/filter dependencies are unsupported");
	}
	if(dynamic&&!terminated)return od_fail(reason,"Unterminated ELF dynamic table");
	if(nneed) {
		int found=0;
		if(!strsize||strsize>65536)return od_fail(reason,"Invalid ELF string table");
		for(i=0;i<count;++i)if(u32(ph[i])==1) {
			uint32_t va=u32(ph[i]+8),size=u32(ph[i]+16);
			if(straddr>=va&&straddr-va<=size&&strsize<=size-(straddr-va)) {
				stroff=u32(ph[i]+4)+(straddr-va);found=1;break;
			}
		}
		if(!found)return od_fail(reason,"ELF string table is outside file-backed loads");
		for(i=0;i<nneed;++i) {
			if(need[i]>=strsize)return od_fail(reason,"Invalid ELF dependency offset");
			for(j=0;j<sizeof(buf);++j) {
				if(need[i]+j>=strsize||read_bytes(r,(uint64_t)stroff+need[i]+j,buf+j,1))
					return od_fail(reason,"Unterminated ELF dependency");
				if(!buf[j])break;
			}
			if(j==sizeof(buf)||!allowed_dep((char *)buf))
				return od_fail(reason,"Unsupported ELF dependency (legacy libc, GPU or private library)");
#ifndef OD_TEST_NO_SYSTEM_DEPS
			{
				char path[256];
				snprintf(path,sizeof(path),"/usr/lib/%s",buf);
				if(access(path,R_OK)) {
					snprintf(path,sizeof(path),"/lib/%s",buf);
					if(access(path,R_OK))return od_fail(reason,"Required native shared library is not installed");
				}
			}
#endif
		}
	}
	return 0;
}
