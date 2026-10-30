/* SPDX-License-Identifier: MPL-2.0 */
#include "treefrog-input-map.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int16_t axes[6]={0};
    assert(treefrog_pad(0,axes)==0);
    const unsigned from[]={LS_GP_BACK,LS_GP_L3,LS_GP_R3,LS_GP_START,LS_GP_UP,LS_GP_RIGHT,
        LS_GP_DOWN,LS_GP_LEFT,LS_GP_LB,LS_GP_RB,LS_GP_X,LS_GP_A,LS_GP_B,LS_GP_Y};
    const unsigned to[]={0,1,2,3,4,5,6,7,10,11,12,13,14,15};
    for(unsigned i=0;i<14;++i) assert(treefrog_pad(1u<<from[i],axes)==(1u<<to[i]));
    axes[0]=-32768;axes[1]=32767;axes[4]=32767;axes[5]=32767;
    assert(treefrog_pad(0,axes)==((1u<<7)|(1u<<6)|(1u<<8)|(1u<<9)));
    axes[0]=14000;axes[1]=-14000;axes[4]=0;axes[5]=0;
    assert(treefrog_pad(0,axes)==0);
    axes[0]=32767;axes[1]=-32768;
    assert(treefrog_pad(0,axes)==((1u<<5)|(1u<<4)));
    assert(treefrog_pad(1u<<LS_GP_GUIDE,(int16_t[6]){0})==(1u<<16));
    puts("TREEFROG_INPUT_MAPPING passed=20");
    return 0;
}
