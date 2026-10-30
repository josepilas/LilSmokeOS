/* SPDX-License-Identifier: AGPL-3.0-only */
#include "port.h"
#include "vm.h"
#include "sw_renderer.h"
#include "atlas.h"
#include "gamepad-state.h"
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
extern int ft_input_fixture_fd(int);

static unsigned checks,failed;
#define CHECK(x) do {++checks;if(!(x)){++failed;fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);}} while(0)

static int vm_key(VMContext* vm,uint32_t* code,int key,unsigned function) {
    code[0]=((uint32_t)OP_PUSHI<<24)|(uint16_t)key;
    code[1]=((uint32_t)OP_CALL<<24)|(GML_TYPE_INT32<<16)|1;code[2]=function;
    code[3]=(uint32_t)OP_RET<<24;
    RValue value=VM_executeCode(vm,0);int result=RValue_toInt32(value);RValue_free(&value);return result;
}
static void begin(Runner* r) {RunnerKeyboard_beginFrame(r->keyboard);RunnerGamepad_beginFrame(r->gamepads);}

int ft_self_test(const char* output) {
    if(!ft_platform_open(true)) return 69;
    DataWin data={0};data.gen8.wadVersion=16;
    uint32_t words[4]={0};
    CodeEntry entry={.present=true,.name="gml_Script_lilsmoke_synthetic_menu",.length=sizeof words};
    Function functions[]={{"keyboard_check_pressed",0,0},{"keyboard_check",0,0},{"keyboard_check_released",0,0}};
    data.code.count=1;data.code.entries=&entry;data.bytecodeBuffer=(uint8_t*)words;
    data.func.functionCount=3;data.func.functions=functions;
    VMContext* vm=VM_create(&data);
    Runner runner={0};runner.dataWin=&data;runner.keyboard=RunnerKeyboard_create();runner.gamepads=RunnerGamepad_create();
    vm->runner=&runner;
    FtPad pad={0};pad.connected=true;
    const int dir_keys[4]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};
    /* Exercise the same beginFrame -> adapter -> VM ordering used by loop.c. */
    for(unsigned d=0;d<4;++d) {
        begin(&runner);pad.buttons[d]=true;ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
        CHECK(vm_key(vm,words,dir_keys[d],0)==1);CHECK(vm_key(vm,words,dir_keys[d],1)==1);
        CHECK(RunnerGamepad_buttonCheckPressed(runner.gamepads,0,GP_PADU+d));
        begin(&runner);ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
        CHECK(vm_key(vm,words,dir_keys[d],0)==0);CHECK(vm_key(vm,words,dir_keys[d],1)==1);
        begin(&runner);pad.buttons[d]=false;ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
        CHECK(vm_key(vm,words,dir_keys[d],2)==1);CHECK(vm_key(vm,words,dir_keys[d],1)==0);
    }
    int cursor=0,confirmed=0;
    begin(&runner);pad.buttons[FT_DOWN]=true;ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
    cursor+=vm_key(vm,words,VK_DOWN,0);CHECK(cursor==1);
    begin(&runner);ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
    cursor+=vm_key(vm,words,VK_DOWN,0);CHECK(cursor==1);
    begin(&runner);pad.buttons[FT_DOWN]=false;pad.buttons[FT_A]=true;
    ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
    confirmed+=vm_key(vm,words,'Z',0);CHECK(confirmed==1);
    CHECK(!RunnerKeyboard_check(runner.keyboard,VK_ENTER));CHECK(!RunnerKeyboard_check(runner.keyboard,VK_SPACE));
    CHECK(RunnerGamepad_buttonCheckPressed(runner.gamepads,0,GP_FACE1));
    begin(&runner);pad.buttons[FT_A]=false;pad.connected=false;
    ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
    CHECK(vm_key(vm,words,'Z',2)==1);CHECK(!RunnerGamepad_isConnected(runner.gamepads,0));
    CHECK(RunnerGamepad_buttonCheck(runner.gamepads,0,GP_FACE1)==false);
    begin(&runner);pad.connected=true;pad.axes[0]=-0.75f;
    ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
    CHECK(vm_key(vm,words,VK_LEFT,0)==1);
    CHECK(RunnerGamepad_axisValue(runner.gamepads,0,GP_AXIS_LH)<-0.7f);
    begin(&runner);memset(&pad,0,sizeof pad);ft_input_apply(runner.keyboard,runner.gamepads,&pad,NULL);
    CHECK(vm_key(vm,words,VK_LEFT,2)==1);
    /* Actual instruction decoding/arithmetic, not an adapter-generated answer. */
    words[0]=((uint32_t)OP_PUSHI<<24)|19;words[1]=((uint32_t)OP_PUSHI<<24)|23;
    words[2]=((uint32_t)OP_ADD<<24)|(GML_TYPE_INT32<<20)|(GML_TYPE_INT32<<16);words[3]=(uint32_t)OP_RET<<24;
    RValue sum=VM_executeCode(vm,0);CHECK(RValue_toInt32(sum)==42);RValue_free(&sum);

    /* Real native reader, including the 16-byte MIPS kernel wire format. */
    int pipes[2];CHECK(pipe(pipes)==0);CHECK(ft_input_fixture_fd(pipes[0])==0);
    struct {int32_t sec,usec;uint16_t type,code;int32_t value;} wire={0,0,EV_KEY,KEY_DOWN,1};
    CHECK(write(pipes[1],&wire,sizeof wire)==sizeof wire);
    begin(&runner);CHECK(!ft_input_poll());ft_input_runner(&runner);
    CHECK(vm_key(vm,words,VK_DOWN,0)==1);
    begin(&runner);CHECK(!ft_input_poll());ft_input_runner(&runner);CHECK(vm_key(vm,words,VK_DOWN,0)==0);
    wire.value=0;CHECK(write(pipes[1],&wire,sizeof wire)==sizeof wire);
    begin(&runner);CHECK(!ft_input_poll());ft_input_runner(&runner);CHECK(vm_key(vm,words,VK_DOWN,2)==1);
    wire.value=1;CHECK(write(pipes[1],&wire,sizeof wire)==sizeof wire);
    begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,VK_DOWN,0)==1);
    close(pipes[1]);begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,VK_DOWN,2)==1);
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_z;
    CHECK(SDL_PushEvent(&e)==1);begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,'Z',0)==1);
    e.type=SDL_KEYUP;CHECK(SDL_PushEvent(&e)==1);begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,'Z',2)==1);

    const char* testdir=getenv("FROGGY_TEST_DIR");
    if(testdir) {
        char path[600];snprintf(path,sizeof path,"%s/pads.state",testdir);
        setenv("LILSMOKE_GAMEPAD_STATE",path,1);
        struct ls_gp_state state={0};state.magic=LS_GP_MAGIC;state.abi=1;state.sequence=2;
        state.millis=ls_gp_now();state.pads[0].connected=1;state.pads[0].buttons=1u<<LS_GP_A;
        int fd=open(path,O_RDWR|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
        if(fd>=0) {
            CHECK(write(fd,&state,sizeof state)==sizeof state);
            SDL_Delay(110); /* Reader's bounded reconnect backoff after the absent OS transport. */
            begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,'Z',0)==1);
            CHECK(RunnerGamepad_buttonCheckPressed(runner.gamepads,0,GP_FACE1));
            state.millis=ls_gp_now()-2000;
            CHECK(pwrite(fd,&state,sizeof state,0)==sizeof state);
            begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,'Z',2)==1);
            memset(state.pads,0,sizeof state.pads);state.sequence=4;state.millis=ls_gp_now();
            state.pads[1].connected=1;state.pads[1].axes[0]=-24576;
            CHECK(pwrite(fd,&state,sizeof state,0)==sizeof state);
            begin(&runner);ft_input_poll();ft_input_runner(&runner);
            CHECK(vm_key(vm,words,VK_LEFT,0)==1);
            CHECK(RunnerGamepad_axisValue(runner.gamepads,0,GP_AXIS_LH)<-0.7f);
            state.sequence=6;state.millis=ls_gp_now();state.pads[1].connected=0;state.pads[1].axes[0]=0;
            CHECK(pwrite(fd,&state,sizeof state,0)==sizeof state);
            begin(&runner);ft_input_poll();ft_input_runner(&runner);CHECK(vm_key(vm,words,VK_LEFT,2)==1);
            close(fd);CHECK(unlink(path)==0);
        }
    }

    Renderer* r=SWRenderer_create();r->vtable->init(r,&data);
    r->vtable->beginFrame(r,320,240,320,240);
    r->vtable->beginGUI(r,320,240,0,0,320,240,-1);
    r->vtable->clearScreen(r,0,1);
    r->vtable->drawRectangle(r,10,10,80,60,0x0000ff,1,false);
    r->vtable->drawRectangle(r,90,10,160,60,0x00ff00,1,false);
    r->vtable->drawRectangle(r,170,10,240,60,0xff0000,1,false);
    r->vtable->drawRectangle(r,12,100+cursor*22,305,120+cursor*22,0x604030,1,false);
    r->vtable->drawTextUI(r,"SYNTHETIC VM MENU",12,78,1,1,0,0xffffff,0xffffff,0xffffff,0xffffff,1,-1);
    r->vtable->drawTextUI(r,"FIRST\nSECOND - CONFIRMED\nTHIRD",24,101,0.5,0.5,0,0xffffff,0xffffff,0xffffff,0xffffff,1,-1);
    r->vtable->endGUI(r);SwRenderer_present();
    CHECK(ft_frames==1);CHECK(ft_pixels[20*320+20]==0xf800);CHECK(ft_pixels[20*320+100]==0x07e0);
    CHECK(ft_pixels[20*320+180]==0x001f);CHECK(ft_pixels[239*320+319]==0);
    unsigned nonzero=0;for(unsigned i=0;i<320*240;++i)nonzero+=ft_pixels[i]!=0;
    CHECK(nonzero>10000);CHECK(nonzero<50000);
    if(output) {
        FILE* f=fopen(output,"wb");CHECK(f!=NULL);
        if(f) {fprintf(f,"P6\n320 240\n255\n");
            bool written=true;for(unsigned i=0;i<320*240;++i) {uint16_t p=ft_pixels[i];unsigned char rgb[3]={
                (unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
                if(fwrite(rgb,1,3,f)!=3)written=false;}
            CHECK(written);
            CHECK(fclose(f)==0);
        }
    }
    r->vtable->destroy(r);VM_free(vm);RunnerKeyboard_free(runner.keyboard);RunnerGamepad_free(runner.gamepads);
    ft_platform_close();
    printf("FROGGY_SELFTEST checks=%u failed=%u vm_menu_cursor=%d vm_confirms=%d frame_hash=%08x nonzero=%u\n",
        checks,failed,cursor,confirmed,ft_frame_hash,nonzero);
    return failed?1:0;
}
