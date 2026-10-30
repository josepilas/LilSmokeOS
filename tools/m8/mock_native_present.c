#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static unsigned games, menus, resets, restored;
static int in_menu;
static uint16_t last_pixel;
int lilsmoke_present_rgb(const void *p,unsigned w,unsigned h,unsigned pitch,int rgb32,unsigned n,unsigned d) {
    assert(p&&w==320&&h==240&&pitch==640&&!rgb32&&n==13333&&d==10000);
    uint16_t pixel=((const uint16_t *)p)[0];
    if(in_menu){assert(pixel==last_pixel);++restored;puts("QA_CACHED_GAME_RESTORED");fflush(stdout);}
    last_pixel=pixel;in_menu=0;++games;
    return 1;
}
int lilsmoke_present_menu(const void *p,unsigned w,unsigned h,unsigned pitch,int rgb32) {
    assert(p&&w>=320&&w<=640&&h==240&&pitch>=w*(rgb32?4U:2U));
    in_menu=1;++menus;
    if(menus==1){puts("QA_MENU_OPEN");fflush(stdout);}
    return 1;
}
void lilsmoke_present_reset(void) { printf("QA_NATIVE_RESET=%u\n",++resets);fflush(stdout); }
void lilsmoke_present_checkpoint(void) {}
__attribute__((destructor)) static void report(void) {
    printf("QA_PRESENT_RESULT games=%u menus=%u resets=%u restored=%u\n",games,menus,resets,restored);
    fflush(stdout);
}
