#include "../../package/lilsmoke-opendingux/src/od.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int memread(void *p,uint64_t at,void *out,size_t n)
{
	memcpy(out,(unsigned char *)p+at,n);return 0;
}
static void put16(unsigned char *p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static void put32(unsigned char *p,unsigned n)
{
	p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);
}
static void valid_elf(unsigned char *b)
{
	memset(b,0,128);memcpy(b,"\177ELF\1\1\1",7);
	put16(b+16,2);put16(b+18,8);put32(b+20,1);put32(b+28,52);put32(b+36,0x70001000);
	put16(b+40,52);put16(b+42,32);put16(b+44,1);
	put32(b+52,1);put32(b+52+16,128);put32(b+52+20,128);
}
int main(void)
{
	struct od_desktop d;struct od_sha s;char hash[65],reason[OD_REASON];
	unsigned char b[128];struct od_reader r={b,128,memread};
	const char *meta="[Desktop Entry]\nType=Application\nName=Native emulator\nExec=app \"two words\" %% %f\nX-LilSmoke-Systems=snes,nes\n";
	od_sha_init(&s);od_sha_final(&s,hash);
	assert(!strcmp(hash,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	od_sha_init(&s);od_sha_update(&s,"abc",3);od_sha_final(&s,hash);
	assert(!strcmp(hash,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	assert(!od_parse(meta,strlen(meta),&d));assert(d.needs_rom&&d.argc==3);
	assert(!strcmp(d.args[1],"two words")&&!strcmp(d.args[2],"%"));
	assert(!od_lex(&d,"/storage/roms/a ;$(literal).sfc"));assert(d.argc==4);
	assert(!strcmp(d.args[3],"/storage/roms/a ;$(literal).sfc"));
	assert(!od_lex(&d,NULL));assert(d.argc==3&&d.needs_rom);
	const char *menu_meta="[Desktop Entry]\nType=Application\nName=Native menu\nExec=app %f\nX-LilSmoke-MenuExec=app --menu\n";
	assert(!od_parse(menu_meta,strlen(menu_meta),&d));
	assert(!strcmp(d.menu_exec,"app --menu"));
	strcpy(d.exec,d.menu_exec);assert(!od_lex(&d,NULL));
	assert(d.argc==2&&!d.needs_rom&&!strcmp(d.args[1],"--menu"));
	const char *duplicate="[Desktop Entry]\nType=Application\nName=Bad\nExec=app\nX-LilSmoke-MenuExec=app\nX-LilSmoke-MenuExec=other\n";
	assert(od_parse(duplicate,strlen(duplicate),&d));
	strcpy(d.exec,"app ; touch x");d.reason[0]=0;assert(od_lex(&d,NULL));
	strcpy(d.exec,"app %x");d.reason[0]=0;assert(od_lex(&d,NULL));
	strcpy(d.exec,"app \"unterminated");d.reason[0]=0;assert(od_lex(&d,NULL));
	strcpy(d.exec,"app prefix%f");d.reason[0]=0;assert(od_lex(&d,NULL));
	assert(!od_relative("../escape"));assert(!od_relative("/absolute"));
	assert(!od_relative("dir/../escape"));assert(!od_relative("dir//x"));
	assert(!od_relative("dir\\x"));assert(!od_relative("file."));
	assert(od_relative("bin/native-app"));
	valid_elf(b);reason[0]=0;assert(!od_elf(&r,reason,1));
	b[5]=2;reason[0]=0;assert(od_elf(&r,reason,1));
	valid_elf(b);put16(b+18,3);reason[0]=0;assert(od_elf(&r,reason,1));
	valid_elf(b);put32(b+36,0x70001020);reason[0]=0;assert(od_elf(&r,reason,1));
	valid_elf(b);put32(b+36,0x80001000);reason[0]=0;assert(od_elf(&r,reason,1));
	valid_elf(b);put32(b+28,0xffffffff);reason[0]=0;assert(od_elf(&r,reason,1));
	puts("OpenDingux SHA256/metadata/argv/path/ELF unit checks passed");return 0;
}
