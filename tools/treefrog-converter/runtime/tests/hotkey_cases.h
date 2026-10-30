/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>

static void hotkey_cases(void) {
    struct exit_hotkey key={0};
    /* A launch-held combo must first be released, including at time zero. */
    for(unsigned t=0;t<=1000;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
    for(unsigned t=1020;t<=1100;t+=20)assert(!exit_hotkey_step(&key,t,0,9,1));
    assert(!exit_hotkey_step(&key,1120,1,9,1));
    for(unsigned t=1140;t<1640;t+=20)assert(!exit_hotkey_step(&key,t,9|0x400,9,1));
    assert(exit_hotkey_step(&key,1640,9|0x400,9,1));
    for(unsigned t=1660;t<=2300;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
    /* Partial release and a short neutral bounce cannot rearm. */
    assert(!exit_hotkey_step(&key,2320,1,9,1));
    assert(!exit_hotkey_step(&key,2340,0,9,1));
    assert(!exit_hotkey_step(&key,2360,9,9,1));
    for(unsigned t=2380;t<=3000;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
    for(unsigned t=3020;t<=3100;t+=20)assert(!exit_hotkey_step(&key,t,0,9,1));
    for(unsigned t=3120;t<3620;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
    assert(exit_hotkey_step(&key,3620,9,9,1));

    /* A broken hold cancels its entire edge until both buttons are released. */
    memset(&key,0,sizeof key);
    for(unsigned t=0;t<=80;t+=20)assert(!exit_hotkey_step(&key,t,0,9,1));
    for(unsigned t=100;t<500;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
    assert(!exit_hotkey_step(&key,500,8,9,1));
    for(unsigned t=520;t<1200;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));

    /* No credit survives a missing sample, long scheduling gap, or rewind. */
    for(unsigned scenario=0;scenario<3;scenario++) {
        memset(&key,0,sizeof key);
        for(unsigned t=2000;t<=2080;t+=20)assert(!exit_hotkey_step(&key,t,0,9,1));
        for(unsigned t=2100;t<=2500;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
        if(scenario==0)assert(!exit_hotkey_step(&key,2520,9,9,0));
        unsigned start=scenario==1?2800:scenario==2?1000:2540;
        for(unsigned t=start;t<=start+1000;t+=20)assert(!exit_hotkey_step(&key,t,9,9,1));
    }
    /* Start+X and unrelated buttons never count as Start+Select. */
    memset(&key,0,sizeof key);
    for(unsigned t=0;t<=80;t+=20)assert(!exit_hotkey_step(&key,t,0,9,1));
    for(unsigned t=100;t<=1000;t+=20)assert(!exit_hotkey_step(&key,t,8|0x1000,9,1));
}
