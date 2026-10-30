#include "extraction.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace portsmoke::sm64;
int main(int argc,char **argv) {
    if(argc!=4)return 64;
    auto original=std::filesystem::path(argv[2]),cache=std::filesystem::path(argv[3]);
    std::string mode=argv[1];ExtractionStatus status;
    if(mode=="cancel-before")status.cancel=true;
    if(mode=="io-failure")std::ofstream(cache/"bob-us-v1.pdat.tmp")<<"unrecognized private bytes";
    std::thread worker([&]{extractGameData(original,cache,status);});
    if(mode=="cancel-mid" || mode=="mutate-input") {
        while(!status.done.load(std::memory_order_acquire) && status.completed.load()==0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(mode=="cancel-mid")status.cancel=true;
        else if(!status.done.load()) {
            std::fstream file(original/"test.rom",std::ios::in|std::ios::out|std::ios::binary);
            file.seekg(4);char byte=0;file.get(byte);file.seekp(4);file.put(byte^1);file.flush();
        }
    }
    worker.join();
    std::cout<<"ExtractionError="<<status.error<<"\nOriginalRemoved="<<status.originalRemoved
             <<" UsedCache="<<status.usedCache<<" Extracted="<<status.extracted.load()<<" Completed="<<status.completed.load()
             <<" Total="<<status.total.load()<<'\n';
    return status.error.empty()?0:1;
}
