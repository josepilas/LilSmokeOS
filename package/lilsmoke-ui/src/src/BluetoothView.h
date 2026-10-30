#pragma once
#include "View.h"
#include "Config.h"
#include <functional>
#include <sys/types.h>

namespace lilsmoke {
class BluetoothView : public View {
public:
    explicit BluetoothView(Config &config):config_(config) {}
    ~BluetoothView() override;
    void onEnter() override;
    void onLeave() override;
    bool allowAutoBackPop() const override { return false; }
    bool acceptsText() const override { return prompt_=="PIN" || prompt_=="PASSKEY"; }
    std::string textTitle() const override { return "Pairing code"; }
    std::string textValue() const override { return pin_; }
    bool textNumeric() const override { return prompt_=="PASSKEY"; }
    void handleText(const std::string &text) override;
    View *handleInput(InputButton,InputEvent) override;
    void render(Renderer &,Font &,const Theme &) override;
    void setOnBack(std::function<View *()> cb) { back_=std::move(cb); }
private:
    friend struct BluetoothViewTestAccess;
    struct Device { std::string address,name; bool paired,connected,audio; };
    Config &config_;
    std::function<View *()> back_;
    std::vector<Device> devices_;
    std::string status_,buffer_,command_,prompt_,promptText_,pin_="000000";
    pid_t child_=-1;
    int output_=-1,input_=-1,selected_=0,action_=0,digit_=0;
    bool powered_=false,adapter_=false,forget_=false,textEdited_=false;
    void run(const std::vector<std::string> &args);
    void poll();
    void line(const std::string &);
    void respond(const std::string &);
    void cancel();
};
}
