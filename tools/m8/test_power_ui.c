#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../board/segam-m8-v6-stock/m8-power-ui.h"
int lilsmoke_present_rgb(const void *pixels,unsigned w,unsigned h,unsigned pitch,int rgb32,unsigned an,unsigned ad) {
    assert(pixels&&w==640&&h==360&&pitch==1280&&!rgb32&&an==16&&ad==9);return 1;
}
int main(int argc,char **argv) {
    assert(argc<=2);
    assert(!strcmp(m8_power_message(M8_POWER_SUCCESS),"Power off operation succeeded"));
    for(unsigned w=320;w<=640;w+=320){
        unsigned h=w==320?240:360;size_t count=(size_t)w*h;
        uint16_t *buffer=malloc((count+2)*2);assert(buffer);buffer[0]=buffer[count+1]=0xbeef;
        for(int state=M8_POWER_BUSY;state<=M8_POWER_BLACK;++state){
            m8_power_render(buffer+1,w,h,state);
            assert(buffer[0]==0xbeef&&buffer[count+1]==0xbeef);
            size_t lit=0;for(size_t i=1;i<=count;++i)if(buffer[i]!=(state==M8_POWER_BLACK?0:0x10c3))++lit;
            assert(state==M8_POWER_BLACK?lit==0:lit>100);
        }
        free(buffer);
    }
    assert(m8_power_show(M8_POWER_SUCCESS));
    if(argc==2){
        uint16_t pixels[640*360];m8_power_render(pixels,640,360,M8_POWER_SUCCESS);
        FILE *f=fopen(argv[1],"wb");assert(f);fputs("P6\n640 360\n255\n",f);
        for(size_t i=0;i<640*360;++i){uint16_t p=pixels[i];unsigned char rgb[]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}
        assert(!fclose(f));
    }
    puts("PASS exact success text, 320/640 bounds, nonblank glyphs, black frame and native RGB565 handoff");return 0;
}
