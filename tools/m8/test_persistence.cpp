#include "FilePersistence.h"
#include "Config.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
static bool failSync=false;
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){if(failSync){errno=EIO;return -1;}return __real_fsync(fd);}
static std::string read(const std::filesystem::path &p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
    assert(argc==2);std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    auto file=root/"stable.conf";
    assert(lilsmoke::durableReplace(file,"MODE=original\n"));
    struct stat before{},after{};assert(!stat(file.c_str(),&before));
    assert(lilsmoke::durableReplace(file,"MODE=original\n"));
    assert(!stat(file.c_str(),&after));assert(before.st_ino==after.st_ino);
    failSync=true;assert(!lilsmoke::durableReplace(file,"MODE=broken\n"));failSync=false;
    assert(read(file)=="MODE=original\n");
    for(auto &entry:std::filesystem::directory_iterator(root))assert(entry.path().filename().string().find(".tmp.")==std::string::npos);
    lilsmoke::Config config;assert(config.load(file));config.set("MODE","changed");
    assert(config.save());assert(read(file).find("MODE=changed")!=std::string::npos);
    std::cout<<"PASS unchanged writes skipped; failed sync preserves previous data; config save is atomic\n";
}
