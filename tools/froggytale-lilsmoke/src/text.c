/* SPDX-License-Identifier: AGPL-3.0-only */
#include "port.h"
typedef signed char GLbyte;
#include "font8x8_basic.h"

/* The game's font depends on its private atlas. Preparation must not use it. */
void ft_text(Renderer* r,const char* text,int x,int y,int scale,uint32_t color) {
    int start=x;
    for(const unsigned char* p=(const unsigned char*)text;*p;++p) {
        if(*p=='\n') {x=start;y+=10*scale;continue;}
        unsigned c=*p<128?*p:'?';
        if(x+8*scale>312) {x=start;y+=10*scale;}
        if(y+8*scale>232)break;
        for(int row=0;row<8;++row)for(int col=0;col<8;++col)if(font8x8_basic[c][row]&(1<<col))
            r->vtable->drawRectangle(r,x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale,color,1,false);
        x+=8*scale;
    }
}
