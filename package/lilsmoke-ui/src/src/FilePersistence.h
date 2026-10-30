#pragma once
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace lilsmoke {
inline bool syncDirectory(const std::filesystem::path &directory) {
    int fd=open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)return false;
    int result=fsync(fd),saved=errno;
    close(fd);
    if(result==0)return true;
    if(saved==EINVAL || saved==ENOTSUP || saved==ENOSYS){sync();return true;}
    errno=saved;return false;
}

inline bool durableReplace(const std::filesystem::path &path,const std::string &data) {
    namespace fs=std::filesystem;
    std::error_code ec;
    auto state=fs::symlink_status(path,ec);
    if(ec && ec!=std::errc::no_such_file_or_directory)return false;
    if(!ec && fs::exists(state) && !fs::is_regular_file(state)){errno=EINVAL;return false;}
    if(!ec && fs::is_regular_file(state) && fs::file_size(path,ec)==data.size() && !ec){
        std::ifstream current(path,std::ios::binary);
        std::string previous(data.size(),'\0');
        if(current.read(previous.data(),previous.size()) && previous==data)return true;
    }
    auto parent=path.parent_path();
    if(parent.empty())parent=".";
    fs::create_directories(parent,ec);
    if(ec){errno=ec.value();return false;}
    std::string temporary=path.string()+".tmp.XXXXXX";
    std::vector<char> name(temporary.begin(),temporary.end());name.push_back(0);
    int fd=mkstemp(name.data());
    if(fd<0)return false;
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    size_t offset=0;bool ok=true;
    while(offset<data.size()){
        ssize_t n=write(fd,data.data()+offset,data.size()-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){ok=false;break;}
        offset+=static_cast<size_t>(n);
    }
    if(ok && fsync(fd)!=0)ok=false;
    int saved=errno;
    if(close(fd)!=0 && ok){ok=false;saved=errno;}
    if(ok && rename(name.data(),path.c_str())!=0){ok=false;saved=errno;}
    if(!ok){unlink(name.data());errno=saved;return false;}
    // Persist directory/FAT allocation metadata after the synced file replacement.
    return syncDirectory(parent);
}
}
