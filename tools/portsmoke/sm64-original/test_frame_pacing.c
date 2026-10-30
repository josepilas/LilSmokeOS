#include <assert.h>
#include <stdio.h>
#include "frame_pacing.h"
int main(void) {
    const uint64_t frequencies[] = {1000,1000000,1000000000};
    for (unsigned i=0;i<3;++i) {
        uint64_t frequency=frequencies[i];
        struct sm64_frame_pacer p={0,0};
        for (unsigned n=1;n<=30000;++n) {
            uint64_t deadline=sm64_frame_deadline(&p,p.deadline,frequency);
            assert(deadline==(uint64_t)n*frequency/30);
        }
        uint64_t slow=p.deadline+frequency/2;
        assert(sm64_frame_deadline(&p,slow,frequency)==slow);
        uint64_t next=sm64_frame_deadline(&p,slow+frequency/100,frequency);
        assert(next>slow+frequency/100 && next-slow<=frequency/30+1);
        uint64_t paused=next+frequency*600;
        assert(sm64_frame_deadline(&p,paused,frequency)==paused);
    }
    puts("PASS exact 30 Hz pacing, slow-frame recovery and pause recovery; no catch-up bursts");
}
