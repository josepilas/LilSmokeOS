/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>

#ifdef HOTKEY_TRANSIENT_DIRECT
static struct exit_input transient_input;
#define TRANSIENT_READER transient_input.reader
#define TRANSIENT_PREVIOUS transient_input.previous
#define TRANSIENT_KEY transient_input.pads[2]
#define TRANSIENT_GENERATION transient_input.generation
#define TRANSIENT_OBSERVED transient_input.observed
static void transient_clock(uint64_t now) {(void)now;}
static int transient_tick(uint64_t now) {return exit_input_step(&transient_input,now,9);}
#else
#define TRANSIENT_READER h.gamepad
#define TRANSIENT_PREVIOUS h.hotkey_previous
#define TRANSIENT_KEY h.hotkeys[2]
#define TRANSIENT_GENERATION h.hotkey_generation
#define TRANSIENT_OBSERVED h.hotkey_observed
static void transient_clock(uint64_t now) {test_millis=now;}
static int transient_tick(uint64_t now) {
    test_millis=now;struct ls_gp_state state;h.stop=false;
    int valid=ls_gp_read(&h.gamepad,&state);
    poll_shared_hotkeys(&state,valid,now);return h.stop;
}
#endif

static void transient_publish(int fd,struct ls_gp_state *state,uint64_t now,int odd) {
    transient_clock(now);state->millis=ls_gp_now();state->sequence+=2;
    if(odd)state->sequence|=1;else state->sequence&=~1u;
    assert(pwrite(fd,state,sizeof *state,0)==sizeof *state);
}
static void transient_cases(void) {
    char path[]="/tmp/treefrog-hotkey-transient-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);assert(setenv("LILSMOKE_GAMEPAD_STATE",path,1)==0);
#ifdef HOTKEY_TRANSIENT_DIRECT
    memset(&transient_input,0,sizeof transient_input);transient_input.reader.fd=-1;
#else
    ls_gp_close(&h.gamepad);memset(h.hotkeys,0,sizeof h.hotkeys);
    memset(&h.hotkey_previous,0,sizeof h.hotkey_previous);h.stop=false;
#endif
    struct ls_gp_state state={.magic=LS_GP_MAGIC,.abi=1,.pid=123};
    state.pads[2].connected=1;state.pads[2].instance=44;
    const uint32_t combo=(1u<<LS_GP_BACK)|(1u<<LS_GP_START);
    for(unsigned t=0;t<=80;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    state.pads[2].buttons=combo;
    for(unsigned t=100;t<=580;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    uint64_t last=TRANSIENT_KEY.last,held=TRANSIENT_KEY.held_at;
    /* The deadline falls inside contention. Cache must neither fire nor advance. */
    for(unsigned t=600;t<=640;t+=20) {
        transient_publish(fd,&state,t,1);assert(!transient_tick(t));
        assert(TRANSIENT_KEY.last==last && TRANSIENT_KEY.held_at==held && TRANSIENT_KEY.armed);
    }
    transient_publish(fd,&state,660,0);assert(transient_tick(660));
    for(unsigned t=680;t<=1200;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    /* Cached neutral cannot rearm during contention or represent a release. */
    state.pads[2].buttons=0;transient_publish(fd,&state,1220,0);assert(!transient_tick(1220));
    for(unsigned t=1240;t<=1320;t+=20){transient_publish(fd,&state,t,1);assert(!transient_tick(t));assert(!TRANSIENT_KEY.armed);}
    state.pads[2].buttons=combo;
    for(unsigned t=1340;t<=2000;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}

    /* TTL expiry, a reader generation change, or >150 ms without valid input
       invalidates a hold even if every sampled packet remains seqlock-odd. */
    for(unsigned scenario=0;scenario<3;scenario++) {
        unsigned base=2200+scenario*2000;state.pads[2].buttons=0;
        for(unsigned t=base;t<=base+80;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
        state.pads[2].buttons=combo;
        for(unsigned t=base+100;t<=base+500;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
        unsigned invalid=base+520;
        transient_publish(fd,&state,invalid,1);
        if(scenario==0)TRANSIENT_PREVIOUS.millis=ls_gp_now()-501;
        if(scenario==1)TRANSIENT_READER.generation++;
        if(scenario==2){invalid=base+680;transient_clock(invalid);}
        assert(!transient_tick(invalid));assert(!TRANSIENT_KEY.armed);
        for(unsigned t=invalid+20;t<=invalid+800;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    }
    /* Replacement is detected even before the reader's 100 ms inode check. */
    state.pads[2].buttons=0;
    for(unsigned t=9000;t<=9080;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    state.pads[2].buttons=combo;
    for(unsigned t=9100;t<=9500;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    transient_publish(fd,&state,9520,1);TRANSIENT_READER.checked=ls_gp_now();
    char replacement[]="/tmp/treefrog-hotkey-replacement-XXXXXX";int newfd=mkstemp(replacement);assert(newfd>=0);
    transient_publish(newfd,&state,9520,0);assert(rename(replacement,path)==0);
    assert(!transient_tick(9520));assert(!TRANSIENT_KEY.armed);
    ls_gp_close(&TRANSIENT_READER);
    close(fd);fd=newfd;
    for(unsigned t=9540;t<=10200;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    /* Explicit unplug is never treated as a transient read. */
    state.pads[2].connected=0;transient_publish(fd,&state,10220,0);assert(!transient_tick(10220));
    state.pads[2].connected=1;
    for(unsigned t=10240;t<=11000;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    /* Returning held from a malformed/future/stale published packet is unarmed. */
    for(unsigned scenario=0;scenario<3;scenario++) {
        unsigned base=12000+scenario*2000;state.pads[2].buttons=0;
        for(unsigned t=base;t<=base+80;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
        state.pads[2].buttons=combo;
        for(unsigned t=base+100;t<=base+500;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
        transient_publish(fd,&state,base+520,0);
        if(scenario==0)state.abi=2;
        if(scenario==1)state.millis=ls_gp_now()+1000;
        if(scenario==2)state.millis=ls_gp_now()-1000;
        assert(pwrite(fd,&state,sizeof state,0)==sizeof state);assert(!transient_tick(base+520));
        assert(!TRANSIENT_KEY.armed);state.abi=1;
        for(unsigned t=base+540;t<=base+1300;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    }
    /* If contention resolves to an even live packet between the two reads,
       recover that packet, including a real release, rather than reuse cache. */
    state.pads[2].buttons=0;
    for(unsigned t=19000;t<=19080;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    state.pads[2].buttons=combo;
    for(unsigned t=19100;t<=19500;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    state.pads[2].buttons=0;transient_publish(fd,&state,19520,0);
    struct ls_gp_state recovered;
    assert(exit_snapshot_pending(&TRANSIENT_READER,&TRANSIENT_PREVIOUS,TRANSIENT_GENERATION,
                                 TRANSIENT_OBSERVED,19520,&recovered)==2);
    assert(recovered.pads[2].buttons==0);
#ifndef HOTKEY_TRANSIENT_DIRECT
    struct ls_gp_state cleared={0};h.stop=false;poll_shared_hotkeys(&cleared,false,19520);
    assert(!h.stop && !TRANSIENT_KEY.holding && !TRANSIENT_KEY.armed);
#else
    assert(!transient_tick(19520));assert(!TRANSIENT_KEY.holding && !TRANSIENT_KEY.armed);
#endif
    state.pads[2].buttons=combo;
    for(unsigned t=19540;t<=20200;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    state.pads[2].buttons=0;state.pads[0].connected=1;
    for(unsigned t=20220;t<=20300;t+=20){transient_publish(fd,&state,t,0);assert(!transient_tick(t));}
    state.pads[0].buttons=1u<<LS_GP_START;state.pads[2].buttons=1u<<LS_GP_BACK;
    for(unsigned t=20320;t<=20900;t+=20) {
        transient_publish(fd,&state,t,t==20500 || t==20520);assert(!transient_tick(t));
    }
    ls_gp_close(&TRANSIENT_READER);close(fd);assert(unlink(path)==0);
#ifndef HOTKEY_TRANSIENT_DIRECT
    h.stop=false;
#endif
    puts("HOTKEY_TRANSIENT collision/deadline/no-cached-fire/release/TTL/gap/generation/inode/unplug/invalid passed");
}
#undef TRANSIENT_READER
#undef TRANSIENT_PREVIOUS
#undef TRANSIENT_KEY
#undef TRANSIENT_GENERATION
#undef TRANSIENT_OBSERVED
