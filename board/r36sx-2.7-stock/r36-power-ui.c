/* SPDX-License-Identifier: MPL-2.0 */
#define m8_power_show m8_power_show_unused_tv
#include "../segam-m8-v6-stock/m8-power-ui.c"
#undef m8_power_show
int m8_power_show(enum m8_power_screen state){
    static uint16_t pixels[640*480];
    m8_power_render(pixels,640,480,state);
    setenv("LILSMOKE_R36_DISPLAY","1",1);
    return lilsmoke_present_rgb(pixels,640,480,1280,0,4,3);
}
