#include <assert.h>
#include <stdio.h>
#include "rk707.h"
int main(void) {
    struct ls_gp_pad pad = {0};
    unsigned char b[27] = {0,0,15,128,128,128,128};
    assert(rk707_decode(b,27,&pad) && !pad.buttons);
    for(unsigned i=0;i<6;++i) assert(pad.axes[i]==0);
    const unsigned raw[] = {4,2,8,1,16,32,256,512,2048,1024};
    const unsigned canonical[] = {0,1,2,3,9,10,4,6,7,8};
    for(unsigned i=0;i<10;++i) {
        b[0]=raw[i]&255;b[1]=raw[i]>>8;
        assert(rk707_decode(b,27,&pad) && pad.buttons==(1U<<canonical[i]));
    }
    b[0]=0xc0;b[1]=0;assert(rk707_decode(b,27,&pad));
    assert(pad.axes[4]==32767 && pad.axes[5]==32767);
    b[0]=0;b[3]=0;b[4]=255;
    assert(rk707_decode(b,27,&pad) && pad.axes[0]==-32768 && pad.axes[1]==32767);
    for(unsigned hat=0;hat<8;++hat) { b[2]=hat;assert(rk707_decode(b,27,&pad) && pad.buttons); }
    b[2]=8;assert(!rk707_decode(b,27,&pad));
    assert(!rk707_decode(b,26,&pad));assert(!rk707_decode(b,28,&pad));
    puts("PASS captured RK707 buttons, triggers, hat directions, calibrated axes, malformed rejection");
}
