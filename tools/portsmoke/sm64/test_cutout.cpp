extern "C" {
#include <TGL/gl.h>
#include <zbuffer.h>
}
#include <array>
#include <iostream>
#include <stdexcept>

void triangle(float z) {
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0,0);glVertex3f(-0.9f,-0.9f,z);
    glTexCoord2f(1,0);glVertex3f(0.9f,-0.9f,z);
    glTexCoord2f(0.5f,1);glVertex3f(0,0.9f,z);
    glEnd();
}
int main() {
    auto *buffer=ZB_open(320,240,ZB_MODE_RGBA,nullptr);
    if(!buffer)return 1;
    glInit(buffer);glViewport(0,0,320,240);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    std::array<unsigned char,64*64*3> key{};
    for(std::size_t i=0;i<64*64;++i){key[i*3]=255;key[i*3+2]=255;}
    glBindTexture(GL_TEXTURE_2D,1);glTexImage2D(GL_TEXTURE_2D,0,3,64,64,0,GL_RGB,GL_UNSIGNED_BYTE,key.data());
    unsigned checks=0;
    for(bool depth:{false,true})for(bool write:{false,true}) {
        if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
        glDepthMask(write?GL_TRUE:GL_FALSE);glClearColor(0.1f,0.25f,0.75f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        auto background=buffer->pbuf[120*320+160];
        glEnable(GL_TEXTURE_2D);glColor3f(0.5f,0.5f,0.5f);triangle(-0.5f);
        if(buffer->pbuf[120*320+160]!=background){std::cerr<<"FAIL cutout wrote shaded key\n";return 1;}++checks;
        glDisable(GL_TEXTURE_2D);glColor3f(1,0,0);triangle(0);
        if(buffer->pbuf[120*320+160]==background){std::cerr<<"FAIL cutout wrote depth\n";return 1;}++checks;
    }
    glClose();ZB_close(buffer);std::cout<<"PASS "<<checks<<" raw-texel cutout/color/depth checks\n";
}
