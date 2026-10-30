#include "od.h"
#include "ini.h"
#include <string.h>

static int field(char *out, size_t cap, const char *v, size_t n)
{
	size_t i, k=0;
	for (i=0;i<n;++i) {
		unsigned char c=(unsigned char)v[i];
		if (!c || c < 32 || c == 127) return -1;
		if (c=='\\') {
			if (++i==n) return -1;
			c=(unsigned char)v[i];
			if(c=='s')c=' ';
			else if(c!='\\') return -1;
		}
		if(k+1>=cap)return -1;
		out[k++]=(char)c;
	}
	out[k]=0;return 0;
}
static int systems(char *out,const char *v,size_t n)
{
	size_t i,k=0,start=0;
	if(n>=256)return -1;
	for(i=0;i<=n;++i) {
		unsigned char c=i<n?(unsigned char)v[i]:0;
		if(!c || c==',' || c==';') {
			if(k==start) {
				if(i==n && i && v[i-1]==';')break;
				if(!n)break;
				return -1;
			}
			if(k-start>24)return -1;
			if(i<n){out[k++]=',';start=k;}
		} else {
			if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return -1;
			out[k++]=(char)c;
		}
	}
	if(k && out[k-1]==',')--k;
	out[k]=0;return 0;
}
int od_parse(const char *buf,size_t len,struct od_desktop *d)
{
	char text[OD_META+32];
	const char *section,*key,*value;
	size_t sn,kn,vn,i;
	struct INI *ini;
	unsigned seen=0,sections=0;
	int r=0,p=0,active=0;
	memset(d,0,sizeof(*d));
	if(!len||len>OD_META||memchr(buf,0,len))return od_fail(d->reason,"Invalid desktop metadata size or NUL");
	/* Extra section prevents libini's legacy trailing-whitespace EOF edge. */
	memcpy(text,buf,len);
	if(text[len-1]!='\n')text[len++]='\n';
	memcpy(text+len,"[OD-End]\n",9);len+=9;
	for(i=0;i<len;++i)
		if((unsigned char)text[i]<32 && text[i]!='\n' && text[i]!='\r' && text[i]!='\t')
			return od_fail(d->reason,"Control byte in desktop metadata");
	ini=ini_open_mem(text,len);
	if(!ini)return od_fail(d->reason,"Desktop parser allocation failed");
	while((r=ini_next_section(ini,&section,&sn))>0) {
		active=sn==13&&!memcmp(section,"Desktop Entry",13);
		if(active && ++sections>1){r=-1;break;}
		while((p=ini_read_pair(ini,&key,&kn,&value,&vn))>0) {
			unsigned bit=0;
			if(!active)continue;
#define KEY(s) (kn==sizeof(s)-1&&!memcmp(key,s,sizeof(s)-1))
			if(KEY("Name")) {bit=1;p=field(d->name,sizeof(d->name),value,vn);}
			else if(KEY("Exec")) {bit=2;p=field(d->exec,sizeof(d->exec),value,vn);}
			else if(KEY("X-LilSmoke-MenuExec")) {bit=64;p=field(d->menu_exec,sizeof(d->menu_exec),value,vn);}
			else if(KEY("Type")) {bit=4;p=vn==11&&!memcmp(value,"Application",11)?0:-1;}
			else if(KEY("X-LilSmoke-Systems")) {bit=8;p=systems(d->systems,value,vn);}
			else if(KEY("X-OpenDingux-Systems")||KEY("X-OpenDingux-System")) {
				bit=16;p=(seen&8)?0:systems(d->systems,value,vn);
			}
			else if(KEY("Terminal")) {bit=32;p=vn==5&&!memcmp(value,"false",5)?0:-1;}
			else if(KEY("Path")||KEY("TryExec")) {p=-1;}
			else if(KEY("Hidden")||KEY("NoDisplay")) {
				p=vn==5&&!memcmp(value,"false",5)?0:-1;
			}
			else p=0;
#undef KEY
			if(p<0 || (bit && (seen&bit))){r=-1;break;}
			seen|=bit;
		}
		if(p<0||r<0){r=-1;break;}
	}
	ini_close(ini);
	if(r<0||sections!=1||(seen&7)!=7||!d->name[0]||!d->exec[0])
		return od_fail(d->reason,"Unsupported or malformed Desktop Entry");
	return od_lex(d,NULL);
}
int od_lex(struct od_desktop *d,const char *rom)
{
	const char *s=d->exec;
	unsigned n=0,k;
	int quoted,started;
	d->needs_rom=0;d->argc=0;
	while(*s) {
		while(*s==' ')++s;
		if(!*s)break;
		if(n>=OD_ARGS-1)return od_fail(d->reason,"Too many Exec arguments");
		k=0;quoted=0;started=0;
		while(*s && (quoted||*s!=' ')) {
			unsigned char c=(unsigned char)*s++;
			started=1;
			if(c=='"'){quoted=!quoted;continue;}
			if(c=='\\') {
				if(!*s)return od_fail(d->reason,"Trailing Exec escape");
				c=(unsigned char)*s++;
				if(!strchr("\"\\$ ",c)&&c!=96)return od_fail(d->reason,"Unsupported Exec escape");
			}
			if(c<32||c==127||(!quoted&&(strchr(";|&<>$",c)||c==96)))
				return od_fail(d->reason,"Shell syntax is not supported in Exec");
			if(k>=OD_ARG-1)return od_fail(d->reason,"Exec argument is too long");
			d->args[n][k++]=(char)c;
		}
		if(quoted||!started)return od_fail(d->reason,"Unclosed Exec quote");
		d->args[n][k]=0;
		if(!strcmp(d->args[n],"%f")||!strcmp(d->args[n],"%F")||
		   !strcmp(d->args[n],"%u")||!strcmp(d->args[n],"%U")) {
			d->needs_rom=1;
			if(rom) {
				if(strlen(rom)>=OD_ARG)return od_fail(d->reason,"ROM path is too long");
				strcpy(d->args[n],rom);
			} else continue;
		} else {
			unsigned a=0,b=0;
			while(d->args[n][a]) {
				char c=d->args[n][a++];
				if(c=='%') {
					if(d->args[n][a++]!='%')return od_fail(d->reason,"Unsupported embedded Exec field code");
				}
				d->args[n][b++]=c;
			}
			d->args[n][b]=0;
		}
		++n;
	}
	d->argc=n;
	if(!n||!d->args[0][0])return od_fail(d->reason,"Exec has no executable");
	return 0;
}
