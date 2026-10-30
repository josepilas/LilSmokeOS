#include "mario_actor.hpp"
extern "C" {
#include <TGL/gl.h>
#include <zbuffer.h>
}
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace portsmoke::sm64;
int main(int argc, char **argv) {
    ZBuffer *buffer = nullptr;
    try {
        if (argc != 5) throw std::runtime_error("usage: mario-preview PRIVATE_PACK MOTION FRAME NEW_PRIVATE_PPM");
        auto actor = loadMarioActor(argv[1]);
        std::string mode = argv[2];
        MarioMotion motion = mode == "idle" ? MarioMotion::Idle : mode == "walk" ? MarioMotion::Walking :
                             mode == "jump" ? MarioMotion::SingleJump : throw std::runtime_error("unknown pose");
        auto pose = sampleMarioPose(actor, motion, std::stof(argv[3]));
        buffer = ZB_open(320, 240, ZB_MODE_RGBA, nullptr);
        if (!buffer) throw std::runtime_error("framebuffer allocation failed");
        glInit(buffer); glViewport(0, 0, 320, 240); glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
        glClearColor(0.12f, 0.16f, 0.22f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-0.49, 0.49, -0.3675, 0.3675, 1, 2000);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        // Actor +Z facing, slightly elevated three-quarter view in LEVEL units.
        glTranslatef(0, -75, -265); glRotatef(12, 1, 0, 0); glRotatef(-25, 0, 1, 0);
        unsigned texture = 1;
        for (const auto& part : actor.parts) {
            unsigned firstTexture = texture;
            for (const auto& item : part.mesh.textures) {
                std::vector<std::uint8_t> rgb(std::size_t(item.image.width) * item.image.height * 3);
                for (unsigned i = 0; i < rgb.size()/3; ++i) std::copy_n(item.image.rgba.data() + i*4, 3, rgb.data()+i*3);
                glBindTexture(GL_TEXTURE_2D, texture++);
                glTexImage2D(GL_TEXTURE_2D, 0, 3, item.image.width, item.image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
            }
            glPushMatrix(); glMultMatrixf(pose.jointMatrices[part.joint].data());
            for (const auto& batch : part.mesh.batches) {
                if (batch.texture >= 0) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, firstTexture + batch.texture); }
                else glDisable(GL_TEXTURE_2D);
                glBegin(GL_TRIANGLES);
                for (unsigned i = 0; i < batch.count; ++i) {
                    const auto& v = part.mesh.vertices[batch.first+i];
                    glColor3f(v.color[0]/255.f, v.color[1]/255.f, v.color[2]/255.f);
                    glTexCoord2f(v.s, v.t); glVertex3f(v.x, v.y, v.z);
                }
                glEnd();
            }
            glPopMatrix();
        }
        unsigned pixels = 0;
        for (unsigned i = 0; i < 320*240; ++i) pixels += buffer->pbuf[i] != buffer->pbuf[319+239*320];
        if (pixels < 1000) throw std::runtime_error("Mario image blank or underframed");
        if (std::filesystem::exists(argv[4])) throw std::runtime_error("private output must not overwrite user data");
        std::ofstream file(argv[4], std::ios::binary);
        file << "P6\n320 240\n255\n";
        for (unsigned i = 0; i < 320*240; ++i) for (unsigned shift : {16u, 8u, 0u}) file.put(char(buffer->pbuf[i] >> shift));
        if (!file) throw std::runtime_error("private preview write failed");
        std::cout << "ActualMarioPose=" << mode << " OriginalAnimation=" << pose.animationId << " Pixels=" << pixels << '\n';
        glClose(); ZB_close(buffer); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
