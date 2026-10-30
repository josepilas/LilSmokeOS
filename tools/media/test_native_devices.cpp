#include "../../package/lilsmoke-media/src/NativeVideo.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(int argc,char **argv){
    assert(argc==2);
    const auto root=std::filesystem::path(argv[1]);
    std::filesystem::create_directory(root);
    const auto alias=(root/"framebuffer-alias").string();
    assert(symlink("/dev/null",alias.c_str())==0);
    assert(lilsmoke::media::device(alias.c_str()));
    int fd=lilsmoke::media::openDevice(alias.c_str(),O_RDONLY);
    assert(fd>=0);struct stat st{};assert(fstat(fd,&st)==0&&S_ISCHR(st.st_mode));close(fd);
    const auto regular=root/"regular";std::ofstream(regular)<<"not a device";
    assert(!lilsmoke::media::device(regular.c_str()));
    assert(lilsmoke::media::openDevice(regular.c_str(),O_RDONLY)==-1);
    const auto regularAlias=(root/"regular-alias").string();
    assert(symlink(regular.c_str(),regularAlias.c_str())==0);
    assert(!lilsmoke::media::device(regularAlias.c_str()));
    assert(lilsmoke::media::openDevice(regularAlias.c_str(),O_RDONLY)==-1);
    assert(!lilsmoke::media::device((root/"missing").c_str()));
    std::puts("PASS native device aliases: character accepted; regular/regular-alias/missing rejected");
}
