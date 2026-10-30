// SPDX-License-Identifier: MPL-2.0
#include "TreeFrogCatalog.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include "application.h"

static std::string setting(const char *name,const char *fallback) {
    const char *s=std::getenv(name); return s && *s ? s : fallback;
}

static bool mips32(const std::string &path) {
    int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return false;
    std::array<unsigned char,52> h{};struct stat st{};
    bool valid=!fstat(fd,&st) && S_ISREG(st.st_mode) && read(fd,h.data(),h.size())==ssize_t(h.size());
    close(fd);
    if(!valid || std::memcmp(h.data(),"\177ELF\1\1\1",7) || h[18]!=8 || h[19] ||
        (h[16]!=2 && h[16]!=3) || h[17])return false;
    unsigned flags=unsigned(h[36])|(unsigned(h[37])<<8)|(unsigned(h[38])<<16)|(unsigned(h[39])<<24);
    unsigned arch=flags>>28;
    return !(flags&0x0620u) && (flags&0xf000u)==0x1000u &&
        (arch==0 || arch==1 || arch==5 || arch==7);
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--application-exec")) {
        try {
            treefrog::ApplicationPlan plan;
            const auto port=treefrog::application("/app", &plan, true);
            treefrog::need(port.launchType=="libretro" && !plan.frontend.empty(), "No independent Libretro launch plan");
            const std::string libraries="/app/runtime/lib:"+port.directory+"/lib:"+port.directory+
                (plan.abi=="glibc"?":/lib:/usr/lib":":/native/lib:/native/usr/lib");
            setenv("LD_LIBRARY_PATH",libraries.c_str(),1);
            unsetenv("LD_PRELOAD");unsetenv("LD_AUDIT");
            setenv("LILSMOKE_APP_DIR","/app",1);
            treefrog::need(!chdir(port.directory.c_str()), "Cannot enter application port tree");
            const std::string frontend="/app/"+plan.frontend;
            const std::string legacy="/mnt/sdcard/roms/ports/"+port.id;
            std::vector<std::string> values{frontend, legacy+'/'+port.binary};
            if(!port.content.empty())values.push_back(legacy+'/'+port.content);
            values.insert(values.end(),{"--profile","/app/runtime/launch.json"});
            std::vector<char*> args;
            for(auto &value:values)args.push_back(value.data());
            args.push_back(nullptr);
            execv(frontend.c_str(),args.data());
            throw std::runtime_error("Cannot execute independent frontend: "+std::string(strerror(errno)));
        } catch(const std::runtime_error &error) {
            std::cerr<<"TreeFrog application: "<<error.what()<<'\n';return 69;
        }
    }
    const bool corePath = argc==3 && !strcmp(argv[1],"--application-core");
    const bool application = argc==3 && (!strcmp(argv[1],"--application") || corePath);
    if(argc!=2 && !application)return 2;
    const auto root=setting("LILSMOKE_TREEFROG_ROOT",lilsmoke::TreeFrogCatalog::DefaultRoot);
    std::string error;
    std::optional<lilsmoke::TreeFrogPort> port;
    treefrog::ApplicationPlan plan;
    try {
        port=application ? treefrog::application(argv[2], &plan) : lilsmoke::TreeFrogCatalog::find(argv[1],root,&error);
    } catch(const std::runtime_error &failure) {
        error=failure.what();
    }
    if(!port || !port->available()) {
        std::cerr<<"TreeFrog Ports: "<<(port?port->reason:error)<<'\n';
        if(port && !port->missingData.empty())std::cerr<<port->missingData<<'\n';
        return 4;
    }
    if(!mips32(port->directory+'/'+port->binary)) {
        std::cerr<<"TreeFrog Ports: not a little-endian MIPS32/o32 engine: "<<port->binary<<'\n';
        return 5;
    }
    const auto stock=application ? "/lilos/cubegm" : setting("LILSMOKE_TREEFROG_STOCK","/lilos/cubegm");
    if((!application || plan.abi!="musl") && (!mips32(stock+"/lib/ld.so.1") || !mips32(stock+"/driver.so"))) {
        std::cerr<<"TreeFrog Ports: treefrog-compat-0.2 requires this device's matching plaintext glibc loader/driver; install the OS platform runtime (not bundled in converted apps)\n";
        return 6;
    }
    if(!application && port->launchType=="libretro" &&
       !mips32((application ? std::string("/storage/.lilsmoke/treefrog-runtime/cubegm") :
           setting("LILSMOKE_TREEFROG_RUNTIME","/storage/.lilsmoke/treefrog-runtime/cubegm"))+"/picoarch")) {
        std::cerr<<"TreeFrog Ports: treefrog-compat-0.2 legacy glibc PicoArch platform runtime is not installed (not bundled in converted apps)\n";
        return 7;
    }
    if(corePath)std::cout<<"/mnt/sdcard/roms/ports/"<<port->id<<'/'<<port->binary<<'\n';
    else if(application)std::cout<<port->id<<' '<<port->launchType<<' '<<(plan.abi.empty()?"glibc":plan.abi)<<'\n';
    return 0;
}
