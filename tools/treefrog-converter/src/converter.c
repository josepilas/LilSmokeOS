/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas. Browser/native semantic port adapter.
 * ZIP and JSON decoding are delegated to pinned miniz and cJSON.
 */
#include "miniz.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, progress, (int stage, double done, double total), {
    if(Module.ontfprogress)Module.ontfprogress(stage,done,total);
});
#else
static void progress(int stage,double done,double total){(void)stage;(void)done;(void)total;}
#endif
#define MAX_FILES 8192
#define MAX_PORTS 128
#define MAX_ZIP (384u*1024u*1024u)
#define MAX_TOTAL (768u*1024u*1024u)
#define MAX_META 65536
#define MAX_ENGINE (32u*1024u*1024u)
int tf_zip_mode(mz_zip_archive*,unsigned);
typedef struct { mz_zip_archive zip; char **names; unsigned count; int open; size_t size,total; } Archive;
typedef struct { char abi[16],needed[64][128]; unsigned n,api; int valid; char error[256]; } Elf;
typedef struct {
    char prefix[1024],id[65],name[121],version[65],profile[16],binary[1024],content[1024],icon[1024];
    char original_binary[1024],script[1024],reason[256],primary_error[256]; Elf elf;
    unsigned auxiliary[32],auxiliary_count; int inferred,normalize_script;
    int valid,side_effects,base_valid,primary_valid,fallback_selected,source_recipe; unsigned config_index,alternatives_examined;
} Port;
static Archive input,pack;
static Port ports[MAX_PORTS];static unsigned port_count;
static char error_text[512];static char *report_text;
static void *output;static size_t output_size;
static int fail(const char *s){if(s!=error_text)snprintf(error_text,sizeof error_text,"%s",s);return 0;}
const char *tf_error(void){return error_text;}
const void *tf_output_data(void){return output;}
size_t tf_output_size(void){return output_size;}
static int same(const char*a,const char*b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
static int starts(const char*s,const char*p){return !strncmp(s,p,strlen(p));}
static int leaf_safe(const char*s){
    size_t n=strlen(s);if(!n||n>120||s[0]=='.'||s[n-1]=='.'||s[n-1]==' ')return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]==127||strchr("/\\:*?\"<>|",s[i]))return 0;
    char base[121];snprintf(base,sizeof base,"%s",s);char*dot=strchr(base,'.');if(dot)*dot=0;
    if(same(base,"CON")||same(base,"PRN")||same(base,"AUX")||same(base,"NUL"))return 0;
    if(strlen(base)==4&&(starts(base,"COM")||starts(base,"LPT"))&&base[3]>='1'&&base[3]<='9')return 0;
    return 1;
}
static int relative(const char*s){
    if(!s||!*s||strlen(s)>1023||s[0]=='/'||strchr(s,'\\'))return 0;
    char leaf[121];size_t n=0;
    for(;;s++){
        if(!*s||*s=='/'){
            if(!n)return 0;leaf[n]=0;if(!leaf_safe(leaf)&&!(leaf[0]=='.'&&leaf_safe(leaf+1)))return 0;n=0;
            if(!*s)return 1;
        }else{if(n>=120)return 0;leaf[n++]=*s;}
    }
}
static int identifier(const char*s){size_t n=strlen(s);if(!n||n>64||!isalnum((unsigned char)s[0]))return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='-')return 0;return 1;}
static int descendant(const char*path,const char*parent){size_t n=strlen(parent);if(strlen(path)<=n||path[n]!='/')return 0;for(size_t i=0;i<n;i++)if(tolower((unsigned char)path[i])!=tolower((unsigned char)parent[i]))return 0;return 1;}
static int version_ok(const char*s){
    if(!*s||strlen(s)>64)return 0;unsigned parts=1;int digit=0;
    for(;*s;s++){
        if(isdigit((unsigned char)*s)){digit=1;continue;}
        if(*s=='.'){if(!digit||++parts>3)return 0;digit=0;continue;}
        if((*s=='-'||*s=='+')&&digit){s++;if(!*s)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='-'&&*s!='.')return 0;return 1;}
        return 0;
    }return digit;
}
static const char*str(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static int copy(char*out,size_t size,const char*s){if(!s||strlen(s)>=size)return 0;memcpy(out,s,strlen(s)+1);return 1;}
static int json_unique(cJSON*j,unsigned depth){
    if(depth>32)return 0;
    for(cJSON*a=j->child;a;a=a->next){
        if(cJSON_IsObject(j))for(cJSON*b=a->next;b;b=b->next)if(a->string&&b->string&&!strcmp(a->string,b->string))return 0;
        if(!json_unique(a,depth+1))return 0;
    }return 1;
}
static cJSON*json_parse(const unsigned char*data,size_t n){
    if(n>MAX_META||memchr(data,0,n))return NULL;
    /* Enforce a shallow budget before the library's recursive parser runs. */
    unsigned depth=0;int quoted=0,escape=0;
    for(size_t i=0;i<n;i++){
        unsigned char c=data[i];
        if(quoted){if(escape){escape=0;continue;}if(c=='\\'){if(i+5<n&&!memcmp(data+i,"\\u0000",6))return NULL;escape=1;continue;}if(c=='"')quoted=0;}
        else if(c=='"')quoted=1;
        else if(c=='{'||c=='['){if(++depth>32)return NULL;}
        else if(c=='}'||c==']'){if(!depth)return NULL;depth--;}
        if(c=='\\'&&i+5<n&&!memcmp(data+i,"\\u0000",6))return NULL;
    }
    if(depth||quoted)return NULL;
    const char*end=NULL;cJSON*j=cJSON_ParseWithLengthOpts((const char*)data,n,&end,0);
    if(!j)return NULL;
    while(end<(const char*)data+n&&isspace((unsigned char)*end))end++;
    if(end!=(const char*)data+n||!json_unique(j,0)){cJSON_Delete(j);return NULL;}return j;
}
/* cJSON stores every number as double; the installed catalog requires a JSON
 * integer token for schema_version. Validate that lexical distinction only. */
static int schema_integer(const unsigned char*b,size_t n){
    unsigned depth=0;int quoted=0,escape=0;size_t start=0;
    for(size_t i=0;i<n;i++){
        unsigned char c=b[i];
        if(quoted){if(escape){escape=0;continue;}if(c=='\\'){escape=1;continue;}if(c=='"'){
            quoted=0;if(depth==1&&i-start==14&&!memcmp(b+start,"schema_version",14)){
                size_t k=i+1;while(k<n&&isspace(b[k]))k++;if(k==n||b[k++]!=':')continue;
                while(k<n&&isspace(b[k]))k++;if(k==n||b[k++]!='1')return 0;
                return k==n||isspace(b[k])||b[k]==','||b[k]=='}';
            }
        }}else if(c=='"'){quoted=1;start=i+1;}else if(c=='{'||c=='[')depth++;else if(c=='}'||c==']')depth--;
    }return 0;
}
/* This is deliberately a small recognized launcher language, not a shell
 * interpreter. Unknown setup semantics are preserved for manual recipes. */
static int launcher_known(Port*p,const char*source,size_t size){
    /* Native shell is preserved and interpreted by BusyBox on the device.
     * Restricting its text to one game's spelling discards valid setup logic.
     * This is trusted program execution, NOT a shell security sandbox. */
    if(!strcmp(p->profile,"native"))return size>0&&!memchr(source,0,size);
    if(memchr(source,0,size)||memchr(source,'\r',size))return 0;
    char*command=calloc(size+2,1);if(!command)return 0;size_t used=0;int execs=0,ok=1;
    for(size_t at=0;at<size&&ok;){
        size_t end=at;while(end<size&&source[end]!='\n')end++;
        const char*s=source+at;size_t len=end-at;while(len&&isspace((unsigned char)*s)){s++;len--;}
        while(len&&isspace((unsigned char)s[len-1]))len--;
        at=end<size?end+1:end;if(!len||*s=='#')continue;
        int continued=s[len-1]=='\\';if(continued)len--;
        if(used){command[used++]=' ';}memcpy(command+used,s,len);used+=len;command[used]=0;
        if(continued)continue;
        size_t write=0;int in_quote=0;
        for(size_t read=0;read<used;read++){
            char c=command[read];if(c=='"')in_quote=!in_quote;
            if(!in_quote&&isspace((unsigned char)c)){if(write&&command[write-1]!=' ')command[write++]=' ';}
            else command[write++]=c;
        }
        while(write&&command[write-1]==' ')write--;command[write]=0;
        char expected[4096];
        if(!strcmp(command,"set -eu")||!strcmp(command,"set -e")||!strcmp(command,"set -u")||
           !strcmp(command,"cd \"$PORT_DIR\"")||!strcmp(command,"rm -f /mnt/sdcard/cubegm/port_error.txt")){}
        else if(starts(command,"PORT_DIR=")){
            snprintf(expected,sizeof expected,"PORT_DIR=/mnt/sdcard/roms/ports/%s",p->id);
            if(strcmp(command,expected)) {snprintf(expected,sizeof expected,"PORT_DIR=\"/mnt/sdcard/roms/ports/%s\"",p->id);if(strcmp(command,expected))ok=0;}
        }else if(starts(command,"if ! /mnt/sdcard/cubegm/port_check.sh ")&&strstr(command,"then exit 0; fi")&&
                 !strstr(command,"$(")&&!strchr(command,'`')&&!strchr(command,'>')&&!strstr(command,"&&")){}
        else if(!strcmp(p->profile,"libretro")){
            snprintf(expected,sizeof expected,"exec /mnt/sdcard/cubegm/picoarch \"$PORT_DIR/%s\"",p->original_binary);
            if(*p->content){size_t n=strlen(expected);snprintf(expected+n,sizeof(expected)-n," \"$PORT_DIR/%s\"",p->content);}
            if(strcmp(command,expected))ok=0;else execs++;
        }else{
            snprintf(expected,sizeof expected,"exec ./%s",p->original_binary);
            if(strcmp(command,expected)){
                snprintf(expected,sizeof expected,"exec \"$PORT_DIR/%s\"",p->original_binary);if(strcmp(command,expected))ok=0;else execs++;
            }else execs++;
        }
        used=0;
    }
    free(command);return ok&&!used&&execs==1;
}
static void archive_close(Archive*a){if(a->open)mz_zip_reader_end(&a->zip);if(a->names)for(unsigned i=0;i<a->count;i++)free(a->names[i]);free(a->names);memset(a,0,sizeof *a);}
void tf_close(void){archive_close(&input);archive_close(&pack);free(report_text);report_text=NULL;free(output);output=NULL;output_size=0;port_count=0;error_text[0]=0;}
static int find(Archive*a,const char*name){for(unsigned i=0;i<a->count;i++)if(!strcmp(a->names[i],name))return (int)i;return -1;}
static void*extract(Archive*a,int i,size_t max,size_t*n){
    mz_zip_archive_file_stat st;if(i<0||!mz_zip_reader_file_stat(&a->zip,(unsigned)i,&st)||st.m_uncomp_size>max)return NULL;
    return mz_zip_reader_extract_to_heap(&a->zip,(unsigned)i,n,0);
}
static int archive_open(Archive*a,const void*data,size_t size,size_t limit){
    if(!data||size<22||size>limit)return fail("ZIP size is outside the supported memory budget");
    if(!mz_zip_reader_init_mem(&a->zip,data,size,0))return fail("Invalid, truncated or unsupported ZIP archive");a->open=1;a->size=size;
    a->count=mz_zip_reader_get_num_files(&a->zip);if(!a->count||a->count>MAX_FILES)return fail("ZIP contains too many entries");
    a->names=calloc(a->count,sizeof(char*));if(!a->names)return fail("Not enough memory for ZIP directory");
    for(unsigned i=0;i<a->count;i++){
        mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&a->zip,i,&st))return fail("Unreadable ZIP directory");
        size_t n=strlen(st.m_filename);int dir=mz_zip_reader_is_file_a_directory(&a->zip,i);
        if(!n||n>=1024||n+1!=mz_zip_reader_get_filename(&a->zip,i,NULL,0))return fail("Invalid or excessively long ZIP filename");
        char path[1024];memcpy(path,st.m_filename,n+1);if(dir&&n&&path[n-1]=='/')path[--n]=0;
        if(!relative(path))return fail("ZIP contains a traversal, reserved or unsafe filename");
        unsigned mode=st.m_external_attr>>16;
        if((mode&0170000)&&((mode&0170000)!=(dir?0040000:0100000)))return fail("Links and special files are not supported");
        if((st.m_bit_flag&1)||(!dir&&st.m_method!=0&&st.m_method!=8))return fail("Encrypted or unsupported ZIP compression");
        if(st.m_uncomp_size>512u*1024u*1024u||st.m_uncomp_size>MAX_TOTAL-a->total)return fail("Expanded ZIP exceeds the 768 MiB budget");
        if(st.m_uncomp_size>1024u*1024u&&st.m_uncomp_size/(st.m_comp_size?st.m_comp_size:1)>1000)return fail("Suspicious ZIP expansion ratio");
        a->total+=(size_t)st.m_uncomp_size;
        for(unsigned k=0;k<i;k++){
            if(same(a->names[k],path))return fail("Duplicate or case-colliding ZIP entries");
            if((!dir&&descendant(a->names[k],path))||
               (!mz_zip_reader_is_file_a_directory(&a->zip,k)&&descendant(path,a->names[k])))return fail("A ZIP file conflicts with a directory path");
        }
        a->names[i]=malloc(n+1);if(!a->names[i])return fail("Not enough memory for filenames");memcpy(a->names[i],path,n+1);
    }return 1;
}
static uint16_t u16(const unsigned char*p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int span(size_t len,size_t at,size_t n){return at<=len&&n<=len-at;}
static const char*table_string(const unsigned char*b,size_t len,size_t at,size_t size,unsigned off){if(off>=size||!span(len,at,size)||!memchr(b+at+off,0,size-off))return NULL;return (const char*)b+at+off;}
static const char*retro_api[]={"retro_init","retro_deinit","retro_api_version","retro_set_environment","retro_set_video_refresh","retro_set_audio_sample","retro_set_audio_sample_batch","retro_set_input_poll","retro_set_input_state","retro_get_system_info","retro_get_system_av_info","retro_set_controller_port_device","retro_reset","retro_run","retro_serialize_size","retro_serialize","retro_unserialize","retro_load_game","retro_unload_game","retro_get_region","retro_get_memory_data","retro_get_memory_size"};
static int elf_offset(const unsigned char*b,size_t n,uint32_t address,size_t size,int executable,size_t*out){
    unsigned phoff=u32(b+28),stride=u16(b+42),count=u16(b+44);
    for(unsigned i=0;i<count;i++){
        const unsigned char*p=b+phoff+i*stride;uint32_t va=u32(p+8),bytes=u32(p+16);
        if(u32(p)!=1||(executable&&!(u32(p+24)&1))||address<va)continue;
        uint64_t delta=(uint64_t)address-va,offset=(uint64_t)u32(p+4)+delta;
        if(delta<=bytes&&size<=bytes-delta&&offset<=n&&size<=n-offset){*out=(size_t)offset;return 1;}
    }return 0;
}
static Elf elf_read(const unsigned char*b,size_t n,int libretro){
    Elf e={0};strcpy(e.abi,"unknown");
    if(n<52||memcmp(b,"\177ELF\1\1\1",7)||u16(b+18)!=8||u32(b+20)!=1||u16(b+40)!=52||(u16(b+16)!=2&&u16(b+16)!=3)||(libretro&&u16(b+16)!=3)){strcpy(e.error,"Engine is not a little-endian ELF32 MIPS executable/core");return e;}
    uint32_t flags=u32(b+36);unsigned arch=flags>>28;
    if((flags&0x620)||((flags&0xf000)!=0x1000)||(arch!=0&&arch!=1&&arch!=5&&arch!=7)){strcpy(e.error,"Engine requires an unsupported MIPS ISA, ABI or FP64 mode");return e;}
    unsigned phoff=u32(b+28),phsize=u16(b+42),phcount=u16(b+44),loads=0;size_t dynamic_at=0,dynamic_size=0;int interpreter_abi=0;
    if(!phcount||phcount>256||phsize<32||!span(n,phoff,(size_t)phsize*phcount)){strcpy(e.error,"ELF program headers are missing or malformed");return e;}
    for(unsigned i=0;i<phcount;i++){
        const unsigned char*s=b+phoff+i*phsize;
        if(u32(s)==1){loads++;if(u32(s+16)>u32(s+20)||!span(n,u32(s+4),u32(s+16))){strcpy(e.error,"ELF load segment is malformed");return e;}}
        if(u32(s)==2){if(dynamic_size||!u32(s+16)||u32(s+16)>65536||u32(s+16)%8||!span(n,u32(s+4),u32(s+16))){strcpy(e.error,"Malformed ELF loader dynamic table");return e;}dynamic_at=u32(s+4);dynamic_size=u32(s+16);}
        if(u32(s)==3){unsigned at=u32(s+4),size=u32(s+16);if(libretro||size<2||size>128||!span(n,at,size)||b[at+size-1]||memchr(b+at,0,size-1)){strcpy(e.error,"Unsupported ELF interpreter");return e;}
            if(!strcmp((const char*)b+at,"/lib/ld.so.1"))interpreter_abi=1;else if(!strcmp((const char*)b+at,"/lib/ld-musl-mipsel.so.1"))interpreter_abi=2;else{strcpy(e.error,"Unrecognized ELF interpreter");return e;}}
    }
    if(!loads){strcpy(e.error,"ELF has no loadable segment");return e;}
    unsigned shoff=u32(b+32),shsize=u16(b+46),count=u16(b+48);int glibc=interpreter_abi==1,musl=interpreter_abi==2;
    if(shsize<40||count>8192||!span(n,shoff,(size_t)shsize*count)){strcpy(e.error,"ELF section table is missing or malformed");return e;}
    uint32_t straddr=0,strsize=0,symaddr=0,syment=0;int terminated=!dynamic_size;
    for(size_t j=0;j<dynamic_size;j+=8){uint32_t tag=u32(b+dynamic_at+j),value=u32(b+dynamic_at+j+4);if(!tag){terminated=1;break;}
        if(tag==5)straddr=value;else if(tag==10)strsize=value;else if(tag==6)symaddr=value;else if(tag==11)syment=value;
        else if(tag==0x6ffffffb&&libretro&&(value&0x08000000u)){strcpy(e.error,"A PIE executable is not a loadable Libretro core");return e;}}
    size_t strings=0;if(!terminated||(dynamic_size&&(!straddr||!strsize||!elf_offset(b,n,straddr,strsize,0,&strings)))){strcpy(e.error,"Invalid mapped ELF string table");return e;}
    if(libretro&&(!dynamic_size||!symaddr||syment!=16)){strcpy(e.error,"Libretro core has no mapped dynamic symbol table");return e;}
    for(size_t j=0;j<dynamic_size;j+=8){uint32_t tag=u32(b+dynamic_at+j);if(!tag)break;if(tag!=1)continue;
        const char*name=table_string(b,n,strings,strsize,u32(b+dynamic_at+j+4));
        if(!name||!leaf_safe(name)||strlen(name)>=128||e.n==64){strcpy(e.error,"Unsafe or excessive ELF dependencies");return e;}
        copy(e.needed[e.n++],128,name);if(!strcmp(name,"libc.so.6"))glibc=1;if(!strcmp(name,"libc.so"))musl=1;
        if(starts(name,"libGL")||starts(name,"libEGL")||starts(name,"libvulkan")){strcpy(e.error,"Engine links a GPU API; a software alternative is needed");return e;}
    }
    for(unsigned i=0;i<count;i++){
        const unsigned char*s=b+shoff+i*shsize;unsigned type=u32(s+4),at=u32(s+16),bytes=u32(s+20),link=u32(s+24),entry=u32(s+36);
        if(type!=6&&type!=11)continue;
        if(link>=count||!span(n,at,bytes)){strcpy(e.error,"Malformed ELF dynamic metadata");return e;}
        if(type==6){
            if(entry!=8||bytes%8||at!=dynamic_at||bytes>dynamic_size){strcpy(e.error,"ELF loader and section dynamic views conflict");return e;}
        }else{
            const unsigned char*t=b+shoff+link*shsize;size_t mapped=0;
            if(entry!=16||bytes%16||!symaddr||!elf_offset(b,n,symaddr,bytes,0,&mapped)||mapped!=at||u32(t+16)!=strings||u32(t+20)!=strsize){strcpy(e.error,"ELF dynamic symbol/string views conflict");return e;}
            for(unsigned j=0;j<bytes;j+=16){
                unsigned info=b[at+j+12],visibility=b[at+j+13]&3;
                if(!u16(b+at+j+14)||(info>>4!=1&&info>>4!=2)||(info&15)!=2||(visibility!=0&&visibility!=3))continue;
                size_t code=0;if(!elf_offset(b,n,u32(b+at+j+4),1,1,&code))continue;
                const char*name=table_string(b,n,strings,strsize,u32(b+at+j));if(!name)continue;
                for(unsigned k=0;k<sizeof(retro_api)/sizeof(*retro_api);k++)if(!strcmp(name,retro_api[k]))e.api|=1u<<k;
            }
        }
    }
    if(glibc&&musl){strcpy(e.error,"Mixed glibc/musl dependencies require a manual port");return e;}
    strcpy(e.abi,glibc?"glibc":musl?"musl":"unknown");
    if(libretro&&e.api!=((1u<<(sizeof(retro_api)/sizeof(*retro_api)))-1)){strcpy(e.error,"Required Libretro API exports are missing");return e;}
    if(libretro&&!strcmp(e.abi,"unknown")){strcpy(e.error,"Cannot select a safe runtime for this core's unknown libc ABI");return e;}
    e.valid=1;return e;
}
static int port_file(Port*p,const char*relative_name){char s[2048];size_t a=strlen(p->prefix),b=strlen(relative_name);if(a+b>=sizeof s)return -1;memcpy(s,p->prefix,a);memcpy(s+a,relative_name,b+1);int i=find(&input,s);return i>=0&&!mz_zip_reader_is_file_a_directory(&input.zip,(unsigned)i)?i:-1;}
static int references(const char*script,const char*name){
    size_t length=strlen(name);const char*line=script;
    while(*line){
        const char*end=strchr(line,'\n');if(!end)end=line+strlen(line);
        const char*first=line;while(first<end&&isspace((unsigned char)*first))first++;
        if(first<end&&*first!='#'){
            const char*at=first;
            while((at=strstr(at,name))&&at<end){
                unsigned char after=(unsigned char)at[length];
                if(at+length<=end&&(!after||(!isalnum(after)&&after!='_'&&after!='.'&&after!='-'))&&
                   (at==first||(!isalnum((unsigned char)at[-1])&&at[-1]!='_'&&at[-1]!='.'&&at[-1]!='-')))return 1;
                at++;
            }
        }
        line=*end?end+1:end;
    }return 0;
}
static void native_auxiliaries(Port*p,const char*script){
    if(strcmp(p->profile,"native"))return;
    for(unsigned i=0;i<input.count;i++){
        if(!starts(input.names[i],p->prefix))continue;
        const char*name=input.names[i]+strlen(p->prefix);
        if(!*name||!strcmp(name,p->binary)||!references(script,name))continue;
        size_t size=0;unsigned char*b=extract(&input,(int)i,MAX_ENGINE,&size);
        int executable=b&&size>=18&&!memcmp(b,"\177ELF",4)&&(u16(b+16)==2||u16(b+16)==3);
        free(b);if(!executable)continue;
        if(p->auxiliary_count==32){p->valid=p->base_valid=0;copy(p->reason,sizeof p->reason,"Too many referenced helper executables");return;}
        p->auxiliary[p->auxiliary_count++]=i;
    }
}
static int needs_shell_normalization(const char*text,size_t size){
    if(size>=3&&!memcmp(text,"\357\273\277",3))return 1;
    for(size_t i=0;i+1<size;i++)if(text[i]=='\r'&&text[i+1]=='\n')return 1;
    return 0;
}
static int runtime_frontend(Port*p){char s[128];snprintf(s,sizeof s,"%s/lilretro",p->elf.abi);return pack.open?find(&pack,s):-1;}
static int base_library(const char*s){const char*names[]={"libc.so.6","libm.so.6","libpthread.so.0","libdl.so.2","librt.so.1","libgcc_s.so.1","libstdc++.so.6","libSDL-1.2.so.0","libSDL2-2.0.so.0","libc.so","libatomic.so.1","libz.so.1","libasound.so.2","libusb-1.0.so.0","libpng16.so.16","libjpeg.so.8"};for(unsigned i=0;i<sizeof names/sizeof *names;i++)if(!strcmp(s,names[i]))return 1;return 0;}
static int dependency(Port*p,const char*name,int*from_pack){
    char s[2048];snprintf(s,sizeof s,"%s/lib/%s",p->elf.abi,name);int i=pack.open?find(&pack,s):-1;
    if(i>=0){*from_pack=1;return i;}*from_pack=0;
    const char*dirs[]={"","lib/","libs/"};for(unsigned d=0;d<3;d++){snprintf(s,sizeof s,"%s%s",dirs[d],name);i=port_file(p,s);if(i>=0)return i;}return -1;
}
typedef struct {char name[128];int index,from_pack,external,invalid;} Dependency;
typedef struct {Dependency files[192];unsigned n,missing,bundled;int frontend_valid;char reason[256];} Closure;
static int need(Closure*c,const char*name){
    for(unsigned i=0;i<c->n;i++)if(!strcmp(c->files[i].name,name))return 1;
    if(c->n==192)return 0;copy(c->files[c->n++].name,128,name);return 1;
}
static Closure closure(Port*p){
    Closure c={0};c.frontend_valid=1;
    for(unsigned i=0;i<p->elf.n;i++)need(&c,p->elf.needed[i]);
    for(unsigned i=0;i<p->auxiliary_count;i++){
        size_t n=0;unsigned char*b=extract(&input,(int)p->auxiliary[i],MAX_ENGINE,&n);Elf e={0};
        if(b){e=elf_read(b,n,0);free(b);}
        if(!e.valid||strcmp(e.abi,p->elf.abi)){
            c.frontend_valid=0;copy(c.reason,sizeof c.reason,"Referenced helper executable has an incompatible ELF/libc ABI");continue;
        }
        for(unsigned k=0;k<e.n;k++)if(!need(&c,e.needed[k])){c.frontend_valid=0;copy(c.reason,sizeof c.reason,"Helper dependency graph exceeds its budget");}
    }
    if(!strcmp(p->profile,"libretro")&&runtime_frontend(p)>=0){
        size_t n=0;unsigned char*b=extract(&pack,runtime_frontend(p),MAX_ENGINE,&n);
        Elf e={0};if(b){e=elf_read(b,n,0);free(b);}
        if(!e.valid||(!strcmp(e.abi,"glibc")&&strcmp(p->elf.abi,"glibc"))||(!strcmp(e.abi,"musl")&&strcmp(p->elf.abi,"musl"))){c.frontend_valid=0;copy(c.reason,sizeof c.reason,"Bundled frontend has an incompatible or invalid ELF ABI");}
        else for(unsigned i=0;i<e.n;i++)if(!need(&c,e.needed[i])){c.frontend_valid=0;copy(c.reason,sizeof c.reason,"Dependency graph exceeds its bounded budget");}
    }
    for(unsigned i=0;i<c.n;i++){
        Dependency*d=&c.files[i];d->index=dependency(p,d->name,&d->from_pack);
        if(d->index<0){if(base_library(d->name))d->external=1;else c.missing++;continue;}
        c.bundled++;size_t bytes=0;unsigned char*b=extract(d->from_pack?&pack:&input,d->index,MAX_ENGINE,&bytes);
        Elf e={0};if(b){e=elf_read(b,bytes,0);free(b);}
        if(!e.valid||(!strcmp(e.abi,"glibc")&&strcmp(p->elf.abi,"glibc"))||(!strcmp(e.abi,"musl")&&strcmp(p->elf.abi,"musl"))){d->invalid=1;c.missing++;continue;}
        for(unsigned k=0;k<e.n;k++)if(!need(&c,e.needed[k])){c.frontend_valid=0;copy(c.reason,sizeof c.reason,"Dependency graph exceeds its bounded budget");}
    }return c;
}
/* Only alternate Libretro engines from this port's own family are candidates.
 * This is a bounded strategy search, not a claim of universal binary repair. */
static void family(char*out,size_t cap,const char*name){
    const char*base=strrchr(name,'/');base=base?base+1:name;copy(out,cap,base);
    char*dot=strrchr(out,'.');if(dot)*dot=0;
    const char*markers[]={"_libretro","-libretro","_software","-software","_mipsel","_mips","_sw"};
    int again=1;while(again){again=0;for(unsigned i=0;i<sizeof markers/sizeof *markers;i++){char*m=strstr(out,markers[i]);if(m&&(!m[strlen(markers[i])]||m[strlen(markers[i])]=='_')){*m=0;again=1;}}}
}
static void choose_engine(Port*p){
    if(strcmp(p->profile,"libretro")||!p->base_valid)return;
    char expected[1024];family(expected,sizeof expected,p->original_binary);
    int best=-1000;char chosen[1024]={0};Elf chosen_elf={0};p->alternatives_examined=0;
    for(unsigned pass=0;pass<input.count+1;pass++){
        char candidate[1024];int index;
        if(!pass){copy(candidate,sizeof candidate,p->original_binary);index=port_file(p,candidate);}
        else{
            index=(int)pass-1;const char*full=input.names[index];if(!starts(full,p->prefix))continue;
            const char*rel=full+strlen(p->prefix);size_t len=strlen(rel);if(len<4||strcmp(rel+len-3,".so")||!strcmp(rel,p->original_binary))continue;
            char actual[1024];family(actual,sizeof actual,rel);if(!*actual||strcmp(expected,actual))continue;
            if(p->alternatives_examined++>=16)break;copy(candidate,sizeof candidate,rel);
        }
        size_t size=0;unsigned char*b=extract(&input,index,MAX_ENGINE,&size);if(!b)continue;
        Elf e=elf_read(b,size,1);free(b);if(!pass){p->primary_valid=e.valid;copy(p->primary_error,sizeof p->primary_error,e.error);}
        if(!e.valid)continue;
        Port temporary=*p;temporary.elf=e;copy(temporary.binary,sizeof temporary.binary,candidate);
        Closure graph=closure(&temporary);int score=100-(int)graph.missing*10;
        if(runtime_frontend(&temporary)>=0&&graph.frontend_valid)score+=100;
        if(!pass)score++;if(score>best){best=score;copy(chosen,sizeof chosen,candidate);chosen_elf=e;}
    }
    if(*chosen){copy(p->binary,sizeof p->binary,chosen);p->elf=chosen_elf;p->valid=1;p->reason[0]=0;p->fallback_selected=strcmp(p->binary,p->original_binary)!=0;}
}
static void stage(cJSON*a,const char*name,const char*status,const char*detail){cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"stage",name);cJSON_AddStringToObject(j,"status",status);cJSON_AddStringToObject(j,"detail",detail);cJSON_AddItemToArray(a,j);}
static void refresh_report(void){
    cJSON*r=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(r,"ports");cJSON_AddNumberToObject(r,"files",input.count);cJSON_AddNumberToObject(r,"archive_bytes",input.size);cJSON_AddNumberToObject(r,"expanded_bytes",input.total);
    cJSON_AddStringToObject(r,"runtime_required","Matching LilSmoke device glibc/musl and hardware adapters");cJSON_AddArrayToObject(r,"warnings");
    for(unsigned i=0;i<port_count;i++){
        Port*p=&ports[i];cJSON*j=cJSON_CreateObject();cJSON_AddItemToArray(list,j);
        const char*keys[]={"id","name","version","profile","binary","content","icon","abi"};const char*values[]={p->id,p->name,p->version,p->profile,p->binary,p->content,p->icon,p->elf.abi};
        for(unsigned k=0;k<8;k++)cJSON_AddStringToObject(j,keys[k],values[k]);
        cJSON*deps=cJSON_AddArrayToObject(j,"dependencies"),*missing=cJSON_AddArrayToObject(j,"missing_dependencies"),*external=cJSON_AddArrayToObject(j,"platform_dependencies"),*details=cJSON_AddArrayToObject(j,"dependency_details"),*warnings=cJSON_AddArrayToObject(j,"warnings");Closure graph=closure(p);unsigned miss=graph.missing;
        for(unsigned d=0;d<graph.n;d++){
            Dependency*x=&graph.files[d];cJSON_AddItemToArray(deps,cJSON_CreateString(x->name));
            cJSON*detail=cJSON_CreateObject();cJSON_AddStringToObject(detail,"name",x->name);cJSON_AddStringToObject(detail,"source",x->invalid?"incompatible":x->index>=0?(x->from_pack?"runtime-bundle":"port-bundle"):x->external?"device-abi":"missing");cJSON_AddItemToArray(details,detail);
            if(x->invalid||(!x->external&&x->index<0))cJSON_AddItemToArray(missing,cJSON_CreateString(x->name));
            if(x->external)cJSON_AddItemToArray(external,cJSON_CreateString(x->name));
        }
        int libretro=!strcmp(p->profile,"libretro"),frontend=runtime_frontend(p);
        const char*reason=!p->valid?p->reason:!graph.frontend_valid?graph.reason:miss?"Unresolved or incompatible transitive dependencies":libretro&&frontend<0?"Independent frontend runtime is not available for this ABI":"";
        cJSON_AddBoolToObject(j,"ready",!*reason);cJSON_AddStringToObject(j,"reason",reason);cJSON_AddNumberToObject(j,"bundled_dependencies",graph.bundled);
        cJSON_AddStringToObject(j,"discovery",p->inferred?"ELF and launcher inference":"Archive metadata and ELF inspection");
        cJSON_AddStringToObject(j,"launcher",*p->script?p->script:"run.sh");
        cJSON_AddNumberToObject(j,"helper_executables",p->auxiliary_count);
        cJSON_AddStringToObject(j,"runtime_profile",libretro?(!strcmp(p->elf.abi,"glibc")?"independent-libretro-glibc":"independent-libretro-musl"):"native-legacy-device");
        cJSON_AddStringToObject(j,"readiness",libretro?"Static analysis only; hardware/API probe runs on first launch":"Native setup preserved; BusyBox syntax checked on-device, rendering/audio/controls require a device test");
        cJSON_AddStringToObject(j,"verdict",*reason?"needs-further-adaptation":"ready-for-device-probe");cJSON_AddBoolToObject(j,"proven_impossible",0);
        cJSON*strategies=cJSON_AddArrayToObject(j,"strategies");
        stage(strategies,"Declared engine",p->primary_valid?"accepted":"rejected",p->primary_valid?p->original_binary:p->primary_error);
        stage(strategies,"Compatible alternate engines",p->fallback_selected?"selected":p->alternatives_examined?"examined":"not-found",p->fallback_selected?p->binary:"Same-family MIPS Libretro alternatives in this port archive");
        stage(strategies,"Runtime ABI / dependency providers",(!graph.missing&&graph.frontend_valid&&(!libretro||frontend>=0))?"resolved":"pending","Port libraries, curated runtime pack, then declared device ABI");
        stage(strategies,"Source build recipe",p->source_recipe?"manual-build-available":"not-found",p->source_recipe?"A local build script exists; recompilation requires a matching MIPS SDK":"No supported source build recipe is bundled");
        cJSON_AddItemToArray(warnings,cJSON_CreateString("A failed automated route is not proof that a manual/source adaptation is impossible"));
        if(cJSON_GetArraySize(external))cJSON_AddItemToArray(warnings,cJSON_CreateString("Some libraries are supplied by the device ABI; never mix different libc versions"));
        cJSON_AddItemToArray(warnings,cJSON_CreateString("No instruction translation, engine recompilation or guaranteed frame rate"));
        if(!libretro)cJSON_AddItemToArray(warnings,cJSON_CreateString("Native setup, loops, helpers and saved choices are preserved and executed on-device as trusted code; not a security sandbox"));
        if(p->inferred)cJSON_AddItemToArray(warnings,cJSON_CreateString("Metadata inferred from an unambiguous native ELF/launcher pair; game-data requirements still need device testing"));
        if(p->normalize_script)cJSON_AddItemToArray(warnings,cJSON_CreateString("Native launcher Windows line endings/BOM normalized; exact original retained under runtime/upstream"));
        cJSON*a=cJSON_AddArrayToObject(j,"analysis");
        stage(a,"Discovery","passed",p->inferred?"Native ELF/launcher pair; generated metadata, no named-game template":"Metadata plus actual engine and referenced-helper inspection");
        stage(a,"Layout","passed",p->prefix);stage(a,"ELF / ABI",p->elf.valid?"passed":"blocked",p->elf.valid?p->elf.abi:p->elf.error);
        stage(a,"Libretro exports",libretro?(p->elf.valid?"passed":"blocked"):"not-applicable",libretro?"22 required API symbols checked":"Native engine");
        stage(a,"Dependencies",miss?"blocked":"passed",miss?"Missing/incompatible SONAMEs listed separately":"Transitive graph resolved against port, runtime pack and declared device ABI");
        stage(a,"Launcher",p->side_effects?"blocked":"passed",libretro?"Generated typed launch replaces the legacy shell script":"Original native engine launcher in a validated device view");
        stage(a,"Rendering / Input / Audio adaptation plan","planned",libretro?"CPU-only video capped at 640x480, retropad input, 80 ms audio policy; unsupported core interfaces are probed on-device":"Native engine retains its device interfaces through the compatibility bridge");
        stage(a,"Runtime",libretro&&frontend<0?"blocked":"passed",libretro?"Independent frontend selected by libc ABI":"Device compatibility bridge");
        cJSON*changes=cJSON_AddArrayToObject(j,"adaptations");
        const char*common[]={"Relocate port data into one application folder","Replace fixed SD paths with an application-scoped launch plan","Preserve original engine and license bytes","Use device-owned input and display ABI without Windows input injection"};
        for(unsigned k=0;k<4;k++)cJSON_AddItemToArray(changes,cJSON_CreateString(common[k]));
        if(libretro){cJSON_AddItemToArray(changes,cJSON_CreateString("Replace PicoArch invocation with a bundled independent Libretro frontend"));cJSON_AddItemToArray(changes,cJSON_CreateString("Generate software-video, audio, controller, save and first-run probe policy"));}
    }
    free(report_text);report_text=cJSON_PrintUnformatted(r);cJSON_Delete(r);
}
const char*tf_report(void){return report_text?report_text:"{}";}
static int infer_native_ports(void){
    for(unsigned i=0;i<input.count;i++){
        const char*script=input.names[i];size_t len=strlen(script);
        if(len<3||!same(script+len-3,".sh"))continue;
        const char*slash=strrchr(script,'/');size_t prefix=slash?(size_t)(slash-script)+1:0;
        char config[1200];snprintf(config,sizeof config,"%.*sconfig.json",(int)prefix,script);
        if(find(&input,config)>=0)continue; /* Malformed metadata is not bypassed. */
        int duplicate=0;for(unsigned k=0;k<port_count;k++)if(strlen(ports[k].prefix)==prefix&&!memcmp(ports[k].prefix,script,prefix))duplicate=1;
        if(duplicate)continue;
        if(prefix){char preferred[1200];snprintf(preferred,sizeof preferred,"%.*srun.sh",(int)prefix,script);if(find(&input,preferred)>=0&&strcmp(preferred,script))continue;}
        size_t bytes=0;char*text=extract(&input,(int)i,MAX_META,&bytes);
        if(!text||memchr(text,0,bytes)){free(text);continue;}
        char*body=calloc(bytes+1,1);if(!body){free(text);return fail("Not enough memory for launcher analysis");}memcpy(body,text,bytes);free(text);
        int selected=-1;Elf engine={0};
        for(unsigned j=0;j<input.count;j++){
            const char*path=input.names[j];if(strncmp(path,script,prefix)||strchr(path+prefix,'/')||!references(body,path+prefix))continue;
            size_t n=0;unsigned char*b=extract(&input,(int)j,MAX_ENGINE,&n);
            if(b&&n>=18&&!memcmp(b,"\177ELF",4)&&u16(b+16)==2){Elf e=elf_read(b,n,0);if(e.valid){if(selected>=0){selected=-2;free(b);break;}selected=(int)j;engine=e;}}
            free(b);
        }
        if(selected<0){free(body);continue;}
        char id[128]={0};
        if(prefix){size_t end=prefix-1,start=end;while(start&&script[start-1]!='/')start--;size_t n=end-start;if(n<sizeof id)memcpy(id,script+start,n);}
        else {copy(id,sizeof id,input.names[selected]);char*dot=strrchr(id,'.');if(dot)*dot=0;}
        if(!identifier(id)){free(body);continue;}
        for(unsigned k=0;k<port_count;k++)if(same(ports[k].id,id)){free(body);return fail("Inferred port ID collides with another port");}
        if(port_count==MAX_PORTS){free(body);return fail("Too many inferred ports");}
        Port*p=&ports[port_count++];memset(p,0,sizeof *p);p->config_index=UINT32_MAX;p->inferred=1;
        memcpy(p->prefix,script,prefix);p->prefix[prefix]=0;
        copy(p->id,sizeof p->id,id);copy(p->name,sizeof p->name,id);copy(p->version,sizeof p->version,"0.0.1");
        copy(p->profile,sizeof p->profile,"native");copy(p->binary,sizeof p->binary,input.names[selected]+prefix);
        copy(p->original_binary,sizeof p->original_binary,p->binary);copy(p->script,sizeof p->script,script+prefix);
        p->elf=engine;p->valid=p->base_valid=p->primary_valid=1;p->normalize_script=needs_shell_normalization(body,bytes);native_auxiliaries(p,body);free(body);
        if(port_file(p,"boxart.webp")>=0)strcpy(p->icon,"boxart.webp");else if(port_file(p,"boxart.png")>=0)strcpy(p->icon,"boxart.png");
    }return 1;
}
int tf_open(const void*data,size_t n){
    tf_close();if(!archive_open(&input,data,n,MAX_ZIP)){archive_close(&input);return 0;}
    for(unsigned i=0;i<input.count;i++){
        const char*name=input.names[i];size_t len=strlen(name);if(strcmp(name,"config.json")&&(len<12||strcmp(name+len-12,"/config.json")))continue;
        size_t bytes=0;unsigned char*b=extract(&input,(int)i,MAX_META,&bytes);if(!b)continue;int integer_schema=schema_integer(b,bytes);cJSON*j=json_parse(b,bytes);free(b);if(!j)continue;
        const char*id=str(j,"id"),*title=str(j,"name");cJSON*launch=cJSON_GetObjectItemCaseSensitive(j,"launch");const char*type=str(launch,"type"),*binary=str(launch,"binary"),*content=str(launch,"content");
        if(!id||!identifier(id)||!title||!leaf_safe(title)||!type||(strcmp(type,"libretro")&&strcmp(type,"native"))||!binary||!relative(binary)||(content&&*content&&!relative(content))){cJSON_Delete(j);continue;}
        for(unsigned k=0;k<port_count;k++)if(same(ports[k].id,id)){cJSON_Delete(j);return fail("Duplicate or case-colliding port IDs");}
        cJSON*schema=cJSON_GetObjectItemCaseSensitive(j,"schema_version");if(!integer_schema||!cJSON_IsNumber(schema)||schema->valuedouble!=1){cJSON_Delete(j);continue;}
        if(port_count==MAX_PORTS){cJSON_Delete(j);return fail("Too many ports in one archive");}
        Port*p=&ports[port_count++];memset(p,0,sizeof *p);p->config_index=i;
        copy(p->id,sizeof p->id,id);copy(p->name,sizeof p->name,title);strcpy(p->version,"0.0.1");copy(p->profile,sizeof p->profile,type);copy(p->script,sizeof p->script,"run.sh");copy(p->binary,sizeof p->binary,binary);copy(p->original_binary,sizeof p->original_binary,binary);copy(p->content,sizeof p->content,content?content:"");
        size_t prefix=len-strlen("config.json");memcpy(p->prefix,name,prefix);p->prefix[prefix]=0;
        int engine=port_file(p,p->binary),script=port_file(p,"run.sh");p->valid=1;
        if((engine<0&&strcmp(type,"libretro"))||script<0){p->valid=0;copy(p->reason,sizeof p->reason,"Declared engine or run.sh is missing");}
        cJSON*content_value=cJSON_GetObjectItemCaseSensitive(launch,"content");
        if(content_value&&(!cJSON_IsString(content_value)||!*content_value->valuestring||!relative(content_value->valuestring))){p->valid=0;copy(p->reason,sizeof p->reason,"Invalid declared content path");}
        cJSON*args=cJSON_GetObjectItemCaseSensitive(launch,"args");
        if(args){
            int valid=cJSON_IsArray(args)&&cJSON_GetArraySize(args)<=128;
            for(cJSON*a=args->child;a;a=a->next){if(!cJSON_IsString(a)||strlen(a->valuestring)>1024)valid=0;
                else for(const unsigned char*s=(const unsigned char*)a->valuestring;*s;s++)if(*s<32||*s==127)valid=0;
            }
            if(!valid||(!strcmp(type,"libretro")&&cJSON_GetArraySize(args))){p->valid=0;copy(p->reason,sizeof p->reason,"Launch arguments need a typed adaptation recipe");}
        }
        if(*p->content&&port_file(p,p->content)<0){p->valid=0;copy(p->reason,sizeof p->reason,"Required game content is missing");}
        cJSON*sets=cJSON_GetObjectItemCaseSensitive(j,"required_file_sets");
        if(sets){
            int complete=cJSON_IsArray(sets)&&!cJSON_GetArraySize(sets),valid=cJSON_IsArray(sets)&&cJSON_GetArraySize(sets)<=128;unsigned checked=0;
            for(cJSON*set=sets->child;set;set=set->next){int satisfied=cJSON_IsArray(set)&&cJSON_GetArraySize(set)>0;if(!satisfied)valid=0;
                for(cJSON*file=set->child;file;file=file->next){
                    if(++checked>128||!cJSON_IsString(file)||!relative(file->valuestring)){valid=0;satisfied=0;break;}
                    if(port_file(p,file->valuestring)<0)satisfied=0;
                }if(satisfied)complete=1;
            }
            if(!valid||!complete){p->valid=0;copy(p->reason,sizeof p->reason,valid?"One complete required game-data set must be provided":"Invalid or excessive required_file_sets metadata");}
        }else{p->valid=0;copy(p->reason,sizeof p->reason,"Missing required_file_sets metadata");}
        p->base_valid=p->valid;p->source_recipe=port_file(p,"build.sh")>=0;
        size_t engine_size=0;unsigned char*elf=extract(&input,engine,MAX_ENGINE,&engine_size);
        if(elf){p->elf=elf_read(elf,engine_size,!strcmp(type,"libretro"));free(elf);}else copy(p->elf.error,sizeof p->elf.error,"Engine is unreadable or exceeds the 32 MiB analysis budget");
        if(!p->elf.valid){p->valid=0;copy(p->reason,sizeof p->reason,p->elf.error);}
        p->primary_valid=p->elf.valid;copy(p->primary_error,sizeof p->primary_error,p->elf.error);
        size_t shsize=0;char*sh=extract(&input,script,MAX_META,&shsize);
        if(sh){p->side_effects=!launcher_known(p,sh,shsize);if(!strcmp(type,"native"))p->normalize_script=needs_shell_normalization(sh,shsize);char*body=calloc(shsize+1,1);if(body){memcpy(body,sh,shsize);native_auxiliaries(p,body);free(body);}else p->side_effects=1;free(sh);}else p->side_effects=1;
        if(p->side_effects){p->valid=0;p->base_valid=0;copy(p->reason,sizeof p->reason,"Launcher has setup/side effects requiring a manual adaptation recipe");}
        choose_engine(p);
        if(port_file(p,"boxart.webp")>=0)strcpy(p->icon,"boxart.webp");else if(port_file(p,"boxart.png")>=0)strcpy(p->icon,"boxart.png");
        cJSON_Delete(j);progress(1,i+1,input.count);
    }
    if(!infer_native_ports())return 0;
    if(!port_count)return fail("No valid metadata or unambiguous native ELF/launcher pair was found");
    refresh_report();return 1;
}
int tf_runtime(const void*data,size_t n){archive_close(&pack);if(!archive_open(&pack,data,n,64u*1024u*1024u)){archive_close(&pack);return 0;}for(unsigned i=0;i<port_count;i++)choose_engine(&ports[i]);refresh_report();return 1;}
typedef struct { size_t done,total; } Verify;
static size_t verify_bytes(void*opaque,mz_uint64 offset,const void*data,size_t n){Verify*v=opaque;(void)offset;(void)data;v->done+=n;progress(2,v->done,v->total);return n;}
static int add_text(mz_zip_archive*z,const char*name,const char*text){size_t n=strlen(name);unsigned mode=n>=3&&!strcmp(name+n-3,".sh")?0755:0644;return mz_zip_writer_add_mem(z,name,text,strlen(text),MZ_BEST_SPEED)&&tf_zip_mode(z,mode);}
static int add_json(mz_zip_archive*z,const char*name,cJSON*j){char*s=cJSON_PrintUnformatted(j);int ok=s&&add_text(z,name,s);free(s);cJSON_Delete(j);return ok;}
static int native_metadata(mz_zip_archive*z,Port*p){
    cJSON*j=cJSON_CreateObject(),*launch=cJSON_AddObjectToObject(j,"launch");
    cJSON_AddNumberToObject(j,"schema_version",1);cJSON_AddStringToObject(j,"id",p->id);cJSON_AddStringToObject(j,"name",p->name);
    cJSON_AddStringToObject(launch,"type","native");cJSON_AddStringToObject(launch,"binary",p->binary);cJSON_AddArrayToObject(j,"required_file_sets");
    return add_json(z,"runtime/port/config.json",j);
}
static int script_alias(mz_zip_archive*z,Port*p){
    char path[1200],quoted[5000],wrapper[5200];snprintf(path,sizeof path,"/mnt/sdcard/roms/ports/%s/%s",p->id,p->script);
    size_t used=0;quoted[used++]='\'';
    for(const char*s=path;*s;s++){if(*s=='\''){memcpy(quoted+used,"'\\''",4);used+=4;}else quoted[used++]=*s;}
    quoted[used++]='\'';quoted[used]=0;
    snprintf(wrapper,sizeof wrapper,"#!/bin/sh\n/bin/sh -n %s || exit 65\nexec /bin/sh %s \"$@\"\n",quoted,quoted);
    return add_text(z,"runtime/port/run.sh",wrapper);
}
/* Renaming while preserving compression uses the reader's compressed bytes.
 * CRC is verified before passthrough, without inflating game assets in RAM. */
static int renamed(mz_zip_archive*z,Archive*a,unsigned i,const char*name,Verify*v){
    mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&a->zip,i,&st))return 0;
    if(mz_zip_reader_is_file_a_directory(&a->zip,i))return 1;
    if(!mz_zip_reader_extract_to_callback(&a->zip,i,verify_bytes,v,0))return fail("ZIP data CRC verification failed");
    size_t compressed_size=0;void*compressed=mz_zip_reader_extract_to_heap(&a->zip,i,&compressed_size,MZ_ZIP_FLAG_COMPRESSED_DATA);
    if(!compressed&&st.m_comp_size)return fail("Cannot read compressed ZIP payload");
    int ok=mz_zip_writer_add_mem_ex(z,name,compressed,compressed_size,NULL,0,
        st.m_method==8?MZ_ZIP_FLAG_COMPRESSED_DATA:0,st.m_method==8?st.m_uncomp_size:0,st.m_method==8?st.m_crc32:0);
    size_t len=strlen(name);unsigned mode=(st.m_external_attr>>16)&0111?0755:0644;
    if((len>=3&&!strcmp(name+len-3,".sh"))||!strcmp(name,"runtime/frontend/lilretro"))mode=0755;
    free(compressed);return ok&&tf_zip_mode(z,mode);
}
int tf_convert(unsigned selected,const char*title,const char*version){
    error_text[0]=0;free(output);output=NULL;output_size=0;
    if(selected>=port_count||!title||!leaf_safe(title)||!version||!version_ok(version))return fail("Invalid application selection, name or version");
    Port*p=&ports[selected];if(!p->valid)return fail(p->reason);
    int libretro=!strcmp(p->profile,"libretro"),frontend=runtime_frontend(p);if(libretro&&frontend<0)return fail("No matching independent frontend runtime is attached");
    Closure graph=closure(p);if(graph.missing||!graph.frontend_valid)return fail(*graph.reason?graph.reason:"A transitive dependency is unresolved or incompatible");
    mz_zip_archive writer={0};if(!mz_zip_writer_init_heap(&writer,0,0))return fail("Not enough memory for output archive");int ok=1;Verify verify={0,input.total};
    for(unsigned i=0;i<input.count&&ok;i++){
        if(p->fallback_selected&&i==p->config_index)continue;
        if(starts(input.names[i],p->prefix)&&strlen(input.names[i])>strlen(p->prefix)){
            const char*relative_name=input.names[i]+strlen(p->prefix);
            char name[1200];snprintf(name,sizeof name,"runtime/port/%s",relative_name);
            if(p->normalize_script&&!strcmp(relative_name,p->script)){
                size_t bytes=0;char*raw=extract(&input,(int)i,MAX_META,&bytes),*text=raw?calloc(bytes+1,1):NULL;
                if(!text){free(raw);ok=0;break;}
                size_t read=bytes>=3&&!memcmp(raw,"\357\273\277",3)?3:0,write=0;
                for(;read<bytes;read++){if(raw[read]=='\r'&&read+1<bytes&&raw[read+1]=='\n')continue;text[write++]=raw[read];}
                ok=add_text(&writer,name,text)&&renamed(&writer,&input,i,"runtime/upstream/launcher.original.sh",&verify);free(raw);free(text);
            }else ok=renamed(&writer,&input,i,name,&verify);
            if(ok&&i==(unsigned)port_file(p,p->binary))ok=tf_zip_mode(&writer,0755);
        }
        else if(!strchr(input.names[i],'/')&&
            (starts(input.names[i],"LICENSE")||starts(input.names[i],"COPYING")||
             starts(input.names[i],"NOTICE")||starts(input.names[i],"README"))){
            char name[1200];snprintf(name,sizeof name,"runtime/upstream/%s",input.names[i]);ok=renamed(&writer,&input,i,name,&verify);
        }
    }
    if(p->inferred&&ok){ok=native_metadata(&writer,p);if(ok&&strcmp(p->script,"run.sh"))ok=script_alias(&writer,p);}
    if(p->fallback_selected&&ok){
        size_t bytes=0;unsigned char*b=extract(&input,(int)p->config_index,MAX_META,&bytes);cJSON*j=b?json_parse(b,bytes):NULL;
        if(j){cJSON*launch=cJSON_GetObjectItemCaseSensitive(j,"launch");cJSON_ReplaceItemInObjectCaseSensitive(launch,"binary",cJSON_CreateString(p->binary));ok=add_json(&writer,"runtime/port/config.json",j);}
        else ok=0;
        if(b){ok=ok&&mz_zip_writer_add_mem(&writer,"runtime/upstream/config.original.json",b,bytes,MZ_BEST_SPEED);free(b);}
    }
    if(libretro&&ok)ok=renamed(&writer,&pack,(unsigned)frontend,"runtime/frontend/lilretro",&verify);
    for(unsigned d=0;d<graph.n&&ok;d++){Dependency*x=&graph.files[d];if(x->index>=0){char path[200];snprintf(path,sizeof path,"runtime/lib/%s",x->name);ok=renamed(&writer,x->from_pack?&pack:&input,(unsigned)x->index,path,&verify);}}
    for(unsigned i=0;i<pack.count&&ok&&(libretro||graph.bundled);i++)if(starts(pack.names[i],"licenses/")||starts(pack.names[i],"source/")||!strcmp(pack.names[i],"SOURCE-OFFER.txt")||!strcmp(pack.names[i],"manifest.json")){char name[1200];snprintf(name,sizeof name,"runtime/%s",pack.names[i]);ok=renamed(&writer,&pack,i,name,&verify);}
    if(!ok){mz_zip_writer_end(&writer);return fail(*error_text?error_text:"Cannot construct archive entries");}
    cJSON*meta=cJSON_CreateObject();char identity[128];snprintf(identity,sizeof identity,"org.lilsmoke.treefrog.%s",p->id);
    cJSON_AddStringToObject(meta,"id",identity);cJSON_AddStringToObject(meta,"name",title);cJSON_AddStringToObject(meta,"version",version);cJSON_AddStringToObject(meta,"architecture","mipsel-linux-musl");
    cJSON_AddStringToObject(meta,"description","Adapted TreeFrog port with an application-scoped runtime");
    if(*p->icon){char icon[1200];snprintf(icon,sizeof icon,"runtime/port/%s",p->icon);cJSON_AddStringToObject(meta,"icon",icon);}
    ok=ok&&add_json(&writer,"meta.json",meta);
    cJSON*adapter=cJSON_CreateObject();cJSON_AddNumberToObject(adapter,"schema",1);cJSON_AddStringToObject(adapter,"kind","treefrog-converted");cJSON_AddStringToObject(adapter,"port_id",p->id);cJSON_AddStringToObject(adapter,"profile",p->profile);cJSON_AddStringToObject(adapter,"binary",p->binary);cJSON_AddStringToObject(adapter,"content",p->content);cJSON_AddStringToObject(adapter,"port_root","runtime/port");cJSON_AddStringToObject(adapter,"requires_runtime","treefrog-compat-0.2");cJSON_AddBoolToObject(adapter,"binary_recompiled",0);
    if(libretro){cJSON_AddBoolToObject(adapter,"independent_libretro",1);cJSON_AddStringToObject(adapter,"frontend","runtime/frontend/lilretro");cJSON_AddStringToObject(adapter,"runtime_profile",p->elf.abi);}
    ok=ok&&add_json(&writer,"adapter.json",adapter);
    cJSON*policy=cJSON_CreateObject();cJSON_AddNumberToObject(policy,"schema",1);cJSON_AddStringToObject(policy,"port_id",p->id);cJSON_AddStringToObject(policy,"renderer","software");cJSON_AddNumberToObject(policy,"max_width",640);cJSON_AddNumberToObject(policy,"max_height",480);cJSON_AddNumberToObject(policy,"audio_latency_ms",80);cJSON_AddStringToObject(policy,"input","retropad");cJSON_AddStringToObject(policy,"save_directory","userdata");cJSON_AddBoolToObject(policy,"hardware_rendering",0);cJSON_AddBoolToObject(policy,"probe_on_first_launch",1);cJSON_AddObjectToObject(policy,"core_options");
    ok=ok&&add_json(&writer,"runtime/launch.json",policy);
    char launcher[128];snprintf(launcher,sizeof launcher,"%s.sh",p->id);
    const char*script="#!/bin/sh\n# Generated by LilSmoke Port Converter; arbitrary root launcher name.\nset -eu\n: \"${LILSMOKE_APP_DIR:?Application launch context missing}\"\nhelper=/usr/lib/lilsmoke/lilsmoke-treefrog-launch\nif [ ! -x \"$helper\" ]; then\n  printf '%s\\n' 'Install LilSmoke TreeFrog compatibility 0.2 or newer' >&2\n  exit 69\nfi\nexec \"$helper\" --application \"$LILSMOKE_APP_DIR\"\n";
    ok=ok&&add_text(&writer,launcher,script);refresh_report();
    cJSON*r=report_text?cJSON_Parse(report_text):NULL;if(r){cJSON_AddBoolToObject(r,"binary_recompiled",0);cJSON_AddStringToObject(r,"selected_port",p->id);cJSON_AddStringToObject(r,"validation_scope","Static analysis and CRC checks; on-device probe still required");ok=ok&&add_json(&writer,"CONVERSION-REPORT.json",r);}else ok=0;
    ok=ok&&mz_zip_writer_finalize_heap_archive(&writer,&output,&output_size);mz_zip_writer_end(&writer);
    if(!ok){free(output);output=NULL;output_size=0;return fail(*error_text?error_text:"Could not construct the application archive");}
    progress(3,output_size,output_size);return 1;
}
