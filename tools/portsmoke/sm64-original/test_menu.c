/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdlib.h>
#include "menu.h"

int main(int argc,char **argv) {
    assert(argc==2);
    struct sm64_menu m={.quality=0,.volume=100};
    assert(sm64_menu_step(&m,SM64_MENU_DOWN,argv[1])==0 && m.row==1);
    assert(sm64_menu_step(&m,0,argv[1])==0);
    assert(sm64_menu_step(&m,SM64_MENU_RIGHT,argv[1])==3 && m.quality==1);
    assert(sm64_menu_step(&m,SM64_MENU_RIGHT,argv[1])==0 && m.quality==1);
    assert(sm64_menu_step(&m,0,argv[1])==0);
    assert(sm64_menu_step(&m,SM64_MENU_A,argv[1])==3 && m.quality==2);
    assert(sm64_menu_step(&m,0,argv[1])==0);
    assert(sm64_menu_step(&m,SM64_MENU_B,argv[1])==1);
    m.row=3;m.previous=0;
    assert(sm64_menu_step(&m,SM64_MENU_LEFT,argv[1])==3 && m.volume==95);
    m.previous=0;m.row=2;
    assert(sm64_menu_step(&m,SM64_MENU_RIGHT,"/nonexistent-lilsmoke-parent/menu-test")==0 && !m.smooth && m.error);
    m.previous=0;m.row=4;
    assert(sm64_menu_step(&m,SM64_MENU_A,argv[1])==2);
    sm64_scale_pixel *memory=malloc((320*240+2)*sizeof(*memory));assert(memory);
    memory[0]=memory[320*240+1]=123;
    for (unsigned row=0;row<5;++row) for (unsigned q=0;q<5;++q) {
        m.row=row;m.quality=q;sm64_menu_render(&m,memory+1);
        assert(memory[0]==123 && memory[320*240+1]==123);
    }
    char path[1024];snprintf(path,sizeof(path),"%s/menu.ppm",argv[1]);
    FILE *file=fopen(path,"wb");assert(file);fputs("P6\n320 240\n255\n",file);
    for (unsigned i=0;i<320*240;++i) {
#ifdef CONVERT
        unsigned p=memory[i+1];unsigned char rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
#else
        unsigned p=memory[i+1];unsigned char rgb[3]={p>>16,p>>8,p};
#endif
        assert(fwrite(rgb,1,3,file)==3);
    }
    assert(!fclose(file));free(memory);
    puts("PASS menu navigation/held buttons/config atomic writes/error rollback/exit/render bounds");
}
