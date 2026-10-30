/* SPDX-License-Identifier: MPL-2.0 */
#define main treefrog_bridge_main
#include "../../package/lilsmoke-scripts/src/lilsmoke-treefrog-input.c"
#undef main
#include "../treefrog-converter/runtime/tests/hotkey_cases.h"
#define HOTKEY_TRANSIENT_DIRECT
#include "../treefrog-converter/runtime/tests/hotkey_transient_cases.h"
#include <assert.h>

void treefrog_input_init(void) {}
uint32_t treefrog_input_read(void) {return 0;}
void treefrog_input_close(void) {}

static void publish(int fd,struct ls_gp_state *state) {
    state->millis=ls_gp_now();state->sequence+=2;
    assert(pwrite(fd,state,sizeof *state,0)==sizeof *state);
}
static void transport_cases(void) {
    char path[]="/tmp/treefrog-hotkey-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);
    assert(setenv("LILSMOKE_GAMEPAD_STATE",path,1)==0);
    struct ls_gp_state state={.magic=LS_GP_MAGIC,.abi=1,.pid=100};
    state.pads[3].connected=1;state.pads[3].instance=55;
    struct exit_input input={.reader=LS_GP_READER_INIT};
    for(unsigned t=0;t<=80;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,0));}
    /* Raw slot 4 works even if mapped input omits or remaps the buttons. */
    state.pads[3].buttons=(1u<<LS_GP_BACK)|(1u<<LS_GP_START)|(1u<<LS_GP_X);
    state.pads[3].exported=1;
    for(unsigned t=100;t<600;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,0));}
    publish(fd,&state);assert(exit_input_step(&input,600,0));
    for(unsigned t=620;t<=1200;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,9));}
    /* Same-slot replacement held at insertion is not a new press. */
    state.pads[3].instance++;
    for(unsigned t=1220;t<=2000;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,9));}
    state.pads[3].buttons=0;
    for(unsigned t=2020;t<=2100;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,0));}
    state.pads[3].buttons=(1u<<LS_GP_START);state.pads[0].connected=1;
    state.pads[0].buttons=(1u<<LS_GP_BACK);
    for(unsigned t=2120;t<=2800;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,9));}
    /* A stale transport cancels timing; recovery held must be released. */
    state.millis=ls_gp_now()-2000;assert(pwrite(fd,&state,sizeof state,0)==sizeof state);
    assert(!exit_input_step(&input,2820,0));
    state.pads[3].buttons|=1u<<LS_GP_BACK;
    for(unsigned t=2840;t<=3600;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,9));}
    /* With no shared controllers, the existing mapped legacy word works. */
    state.pads[0].connected=state.pads[3].connected=0;
    for(unsigned t=3620;t<=3700;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,0));}
    for(unsigned t=3720;t<4220;t+=20){publish(fd,&state);assert(!exit_input_step(&input,t,9|0x100));}
    publish(fd,&state);assert(exit_input_step(&input,4220,9|0x100));
    ls_gp_close(&input.reader);close(fd);assert(unlink(path)==0);
}
static void supervision_case(void) {
    char script[]="/tmp/treefrog-hotkey-engine-XXXXXX",report[]="/tmp/treefrog-hotkey-cleanup-XXXXXX";
    int fd=mkstemp(script),record=mkstemp(report);assert(fd>=0 && record>=0);close(record);
    FILE *f=fdopen(fd,"w");assert(f);
    fprintf(f,"trap 'printf cleaned >> %s; sleep .1; exit 0' TERM\nkill -TERM $PPID\nwhile :; do sleep 30; done\n",report);
    assert(fclose(f)==0);
    stopping=0;uint64_t began=ls_gp_now();
    int status=run_engine("/",script);
    if(status)fprintf(stderr,"hotkey supervision status=%d signal=%d elapsed=%llu script=%s report=%s\n",
                      status,(int)stopping,(unsigned long long)(ls_gp_now()-began),script,report);
    assert(status==0);
    assert(stopping==SIGTERM && ls_gp_now()-began<4000);
    char text[16]={0};f=fopen(report,"r");assert(f);assert(fread(text,1,sizeof text-1,f)==7);fclose(f);
    assert(!strcmp(text,"cleaned"));
    assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    /* An engine that ignores TERM is reaped after escalation. */
    f=fopen(script,"w");assert(f);
    assert(fputs("trap '' TERM\nkill -TERM $PPID\nwhile :; do :; done\n",f)>=0);assert(fclose(f)==0);
    stopping=0;began=ls_gp_now();assert(run_engine("/",script)==137);
    assert(ls_gp_now()-began>=800 && ls_gp_now()-began<4000);
    assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    /* Detached grandchildren are adopted and reaped before return. */
    f=fopen(script,"w");assert(f);
    assert(fputs("setsid /bin/sh -c 'sleep 30' &\nsleep .05\nexit 7\n",f)>=0);assert(fclose(f)==0);
    stopping=0;began=ls_gp_now();assert(run_engine("/",script)==7);
    assert(ls_gp_now()-began<4000);
    assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    unlink(script);unlink(report);stopping=0;
}
int main(void) {
    hotkey_cases();transport_cases();transient_cases();supervision_case();
    puts("TREEFROG_HOTKEY state/compound/slot/release/stale/replacement/mapped/supervision passed");
    return 0;
}
