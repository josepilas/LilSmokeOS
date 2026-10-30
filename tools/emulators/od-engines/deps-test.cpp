#include <lcf/ldb/reader.h>
#include <fmt/format.h>
#include <ini.h>
#include <pixman.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

static int setting(void *userdata,const char *section,const char *name,const char *value)
{
    if(std::string(section)=="od"&&std::string(name)=="ready"&&std::string(value)=="1")
        ++*static_cast<int *>(userdata);
    return 1;
}

int main()
{
    lcf::rpg::Database original;
    original.switches.emplace_back();
    original.switches.back().ID=1;
    original.switches.back().name="Synthetic OD dependency test";
    std::stringstream data(std::ios::in|std::ios::out|std::ios::binary);
    assert(lcf::LDB_Reader::Save(data,original,"windows-1252"));
    data.seekg(0);
    auto decoded=lcf::LDB_Reader::Load(data,"windows-1252");
    assert(decoded&&decoded->switches.size()==1);
    assert(decoded->switches[0].ID==1&&decoded->switches[0].name==original.switches[0].name);
    std::stringstream invalid("not an LDB");
    assert(!lcf::LDB_Reader::Load(invalid));
    assert(fmt::format("ready-{}",1)=="ready-1");
    int parsed=0;
    assert(ini_parse_string("[od]\nready=1\n",setting,&parsed)==0&&parsed==1);
    uint32_t pixel=0xff00ff00;
    auto image=pixman_image_create_bits(PIXMAN_a8r8g8b8,1,1,&pixel,4);
    assert(image&&pixman_image_get_width(image)==1);
    pixman_image_unref(image);
    ogg_stream_state stream;
    assert(ogg_stream_init(&stream,42)==0);
    ogg_stream_clear(&stream);
    vorbis_info info;
    vorbis_info_init(&info);
    vorbis_info_clear(&info);
    assert(png_get_libpng_ver(nullptr));
    FT_Library fonts;
    assert(FT_Init_FreeType(&fonts)==0);
    FT_Done_FreeType(fonts);
    std::puts("PASS MIPS native liblcf round-trip/invalid LDB, fmt, inih, pixman, Ogg/Vorbis, PNG, FreeType");
    std::puts("Synthetic data only; ICU/XML disabled; not EasyRPG Player/gameplay acceptance.");
}
