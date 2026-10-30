#include "BluetoothView.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <csignal>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cerrno>

namespace lilsmoke {
namespace { constexpr const char *actions[]={"Pair","Connect","Disconnect","Trust","Untrust","Forget","Use for audio"}; }
BluetoothView::~BluetoothView() { cancel(); }
void BluetoothView::onLeave() { cancel(); }
void BluetoothView::onEnter() { run({"/usr/bin/lilsmoke-bt","list"}); }
void BluetoothView::cancel() {
    if(child_>0) {
        kill(child_,SIGTERM);
        // A closing view must not leave an agent waiting for invisible input.
        for(int i=0;i<20;++i) {
            if(waitpid(child_,nullptr,WNOHANG)==child_) { child_=-1; break; }
            usleep(10000);
        }
        if(child_>0) { kill(child_,SIGKILL); waitpid(child_,nullptr,0); }
    }
    child_=-1;
    if(output_>=0) close(output_);
    if(input_>=0) close(input_);
    output_=input_=-1; prompt_.clear();
}
void BluetoothView::run(const std::vector<std::string> &args) {
    cancel(); buffer_.clear(); status_="Working...";
    command_=args.size()>1?args[1]:"";
    if(command_=="list") { devices_.clear(); adapter_=false; powered_=false; }
    int in[2],out[2];
    if(pipe(in)!=0) { status_="Could not open Bluetooth job"; return; }
    if(pipe(out)!=0) { close(in[0]); close(in[1]); status_="Could not open Bluetooth job"; return; }
    child_=fork();
    if(child_==0) {
        dup2(in[0],STDIN_FILENO); dup2(out[1],STDOUT_FILENO); dup2(out[1],STDERR_FILENO);
        for(int fd:{in[0],in[1],out[0],out[1]}) close(fd);
        std::vector<char *> argv;
        for(auto &s:args) argv.push_back(const_cast<char *>(s.c_str()));
        argv.push_back(nullptr); execv(argv[0],argv.data());
        const char message[]="ERROR\tBluetooth package is not installed\n";
        const ssize_t written=write(1,message,sizeof(message)-1);
        (void)written; _exit(127);
    }
    close(in[0]); close(out[1]);
    if(child_<0) { close(in[1]); close(out[0]); status_="Could not start Bluetooth job"; return; }
    input_=in[1]; output_=out[0];
    fcntl(output_,F_SETFL,fcntl(output_,F_GETFL)|O_NONBLOCK);
    fcntl(input_,F_SETFL,fcntl(input_,F_GETFL)|O_NONBLOCK);
    fcntl(output_,F_SETFD,FD_CLOEXEC); fcntl(input_,F_SETFD,FD_CLOEXEC);
}
void BluetoothView::line(const std::string &s) {
    std::istringstream stream(s); std::vector<std::string> fields; std::string f;
    while(std::getline(stream,f,'\t')) fields.push_back(f);
    if(fields.empty()) return;
    if(fields[0]=="ADAPTER" && fields.size()>=3) {
        adapter_=!fields[1].empty(); powered_=fields[2]=="1";
        status_=adapter_?(powered_?"Ready":"Radio off"):"No supported Bluetooth adapter";
    } else if(fields[0]=="DEVICE" && fields.size()>=6 && devices_.size()<128) {
        devices_.push_back({fields[1],fields[2],fields[3]=="1",fields[4]=="1",fields[5]=="1"});
    } else if(fields[0]=="ERROR" || fields[0]=="OK") {
        status_=fields.size()>1?fields[1]:fields[0];
    } else if(fields[0]=="CONFIRM" || fields[0]=="PIN" || fields[0]=="PASSKEY" || fields[0]=="DISPLAY") {
        prompt_=fields[0]; promptText_=fields.size()>1?fields[1]:""; pin_=prompt_=="PIN"?"0000":"000000"; digit_=0; textEdited_=false;
    }
}
void BluetoothView::poll() {
    if(child_<=0) return;
    char bytes[1024]; ssize_t n;
    while((n=read(output_,bytes,sizeof(bytes)))>0) {
        buffer_.append(bytes,size_t(n));
        if(buffer_.size()>8192) { status_="Bluetooth response too large"; cancel(); return; }
        size_t end;
        while((end=buffer_.find('\n'))!=std::string::npos) { line(buffer_.substr(0,end)); buffer_.erase(0,end+1); }
    }
    int code;
    if(waitpid(child_,&code,WNOHANG)==child_) {
        child_=-1; close(output_); close(input_); output_=input_=-1; prompt_.clear();
        if(WIFEXITED(code) && WEXITSTATUS(code)==0) {
            if(command_!="list") { run({"/usr/bin/lilsmoke-bt","list"}); return; }
        } else if(status_=="Working...") status_="Bluetooth operation failed";
        selected_=std::min(selected_,int(devices_.size())+2);
    }
}
void BluetoothView::respond(const std::string &s) {
    if(input_<0) return;
    const std::string reply=s+"\n";
    // Do not write to a helper that exited between the render and input polls.
    sigset_t set,previous; sigemptyset(&set); sigaddset(&set,SIGPIPE);
    sigprocmask(SIG_BLOCK,&set,&previous);
    ssize_t n=write(input_,reply.data(),reply.size());
    if(n<0 && errno==EPIPE) { timespec zero{}; sigtimedwait(&set,nullptr,&zero); }
    sigprocmask(SIG_SETMASK,&previous,nullptr);
    prompt_.clear();
}
void BluetoothView::handleText(const std::string &text) {
    if(!acceptsText()) return;
    const size_t limit=prompt_=="PIN"?16:6;
    for(unsigned char c:text) {
        const bool erase=c=='\b' || c==127;
        const bool valid=prompt_=="PIN" ? c>=32 && c<=126 : c>='0' && c<='9';
        if(!erase && !valid) continue;
        // Keep the convenient default until the first actual text edit.
        if(!textEdited_) { pin_.clear(); textEdited_=true; }
        if(erase) { if(!pin_.empty()) pin_.pop_back(); }
        else if(pin_.size()<limit) pin_+=char(c);
        digit_=std::max(0,int(pin_.size())-1);
    }
}
View *BluetoothView::handleInput(InputButton b,InputEvent e) {
    poll();
    if(e!=InputEvent::Pressed && e!=InputEvent::Repeat) return nullptr;
    if(!prompt_.empty()) {
        if(b==InputButton::B) { respond("no"); cancel(); status_="Pairing cancelled"; }
        else if(b==InputButton::A || b==InputButton::Start) {
            if(prompt_=="CONFIRM") respond("yes");
            else if(acceptsText() && !pin_.empty()) respond(pin_);
        } else if(acceptsText() && !pin_.empty()) {
            if(b==InputButton::Left) digit_=std::max(0,digit_-1);
            if(b==InputButton::Right) digit_=std::min(int(pin_.size())-1,digit_+1);
            if(b==InputButton::Up && pin_[digit_]>='0' && pin_[digit_]<='9') pin_[digit_]='0'+(pin_[digit_]-'0'+1)%10;
            if(b==InputButton::Down && pin_[digit_]>='0' && pin_[digit_]<='9') pin_[digit_]='0'+(pin_[digit_]-'0'+9)%10;
        }
        return nullptr;
    }
    if(forget_) {
        forget_=false;
        if(b==InputButton::A && selected_>=3 && selected_-3<int(devices_.size()))
            run({"/usr/bin/lilsmoke-bt","forget",devices_[selected_-3].address});
        return nullptr;
    }
    if(b==InputButton::B) { cancel(); if(back_)return back_(); }
    if(child_>0) return nullptr;
    if(b==InputButton::Up) selected_=std::max(0,selected_-1);
    if(b==InputButton::Down) selected_=std::min(int(devices_.size())+2,selected_+1);
    if(b==InputButton::Left) action_=(action_+6)%7;
    if(b==InputButton::Right) action_=(action_+1)%7;
    if(b==InputButton::X) { run({"/usr/bin/lilsmoke-bt","list"}); return nullptr; }
    if(b!=InputButton::A && b!=InputButton::Start) return nullptr;
    if(selected_==0) {
        if(!config_.getBool("BLUETOOTH_ENABLED",false)) {
            config_.set("BLUETOOTH_ENABLED","on"); config_.save();
            run({"/etc/init.d/S40bluetoothd","start"});
        } else if(powered_) {
            config_.set("BLUETOOTH_ENABLED","off"); config_.save();
            run({"/etc/init.d/S40bluetoothd","stop"});
        } else if(adapter_) run({"/usr/bin/lilsmoke-bt","power","on"});
        else run({"/usr/bin/lilsmoke-bt","list"});
    } else if(selected_==1) run({"/usr/bin/lilsmoke-bt","scan"});
    else if(selected_==2) { std::error_code ec; std::filesystem::remove("/persist/config/bluetooth-audio",ec); status_=ec?"Could not change audio output":"Local audio selected"; }
    else {
        auto &d=devices_[selected_-3];
        if(action_==5) forget_=true;
        else if(action_==6) {
            if(!d.audio || !d.connected) status_="Audio device must be connected";
            else {
                std::ofstream file("/persist/config/bluetooth-audio",std::ios::trunc); file<<d.address<<'\n';
                status_=file?"Bluetooth audio selected":"Could not save audio output";
            }
        } else run({"/usr/bin/lilsmoke-bt",action_==0?"pair":action_==1?"connect":action_==2?"disconnect":action_==3?"trust":"untrust",d.address});
    }
    return nullptr;
}
void BluetoothView::render(Renderer &r,Font &font,const Theme &) {
    poll(); r.clear(0x20242AFF); r.fillRect(0,0,r.width(),38,0x30383FFF);
    font.render(r,"Bluetooth",12,9,0xFFFFFFFF);
    if(prompt_.empty() && !forget_) {
        std::vector<std::string> rows={powered_?"Radio: on":"Radio: off","Discover devices","Audio: local"};
        for(auto &d:devices_) rows.push_back(d.name+"  "+(d.connected?"Connected":d.paired?"Paired":d.address));
        int capacity=std::max(1,(r.height()-130)/28),first=std::max(0,selected_-capacity+1),y=50;
        for(int i=first;i<int(rows.size()) && i<first+capacity;++i,y+=28) {
            if(i==selected_)r.fillRect(12,y,r.width()-24,26,0x315944FF);
            font.renderStyled(r,rows[i],20,y+4,r.width()-40,20,0xE4E8EBFF,"",14);
        }
        font.renderStyled(r,status_,12,r.height()-66,r.width()-24,22,0xE8C46AFF,"",13);
        font.renderStyled(r,selected_>=3?actions[action_]:"",12,r.height()-36,r.width()-24,22,0xE4E8EBFF,"",14);
    }
    if(!prompt_.empty() || forget_) {
        r.fillRect(20,r.height()/2-65,r.width()-40,130,0x30383FFF);
        font.renderStyled(r,forget_?"Forget this device?":prompt_=="DISPLAY"?"Enter this code on the device":prompt_=="CONFIRM"?"Confirm device code":"Pairing code",32,r.height()/2-50,r.width()-64,24,0xFFFFFFFF,"",16);
        font.renderStyled(r,acceptsText()?pin_:promptText_,32,r.height()/2-18,r.width()-64,24,0xE8C46AFF,"",18);
        font.renderStyled(r,"Confirm     Cancel",32,r.height()/2+25,r.width()-64,24,0xFFFFFFFF,"",14);
    }
}
}
