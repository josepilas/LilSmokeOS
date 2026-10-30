#define _GNU_SOURCE
#include "gamepad-state.h"
#include <SDL.h>
#include <SDL_hidapi.h>
#include "rk707.h"
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <glob.h>
#include "owner-lock.h"

static struct ls_owner_lock state_owner = LS_OWNER_LOCK_INIT;
static void release_state_owner(void) { ls_owner_release(&state_owner); }

static int read_unsigned(const char *directory, const char *field, unsigned *value) {
    char path[512], tail;
    if (snprintf(path, sizeof(path), "%s/%s", directory, field) >= (int)sizeof(path)) return 0;
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    int valid = fscanf(file, "%u %c", value, &tail) == 1;
    fclose(file); return valid;
}

static void prepare_usb_nodes(void) {
    /* Stock mdev may omit usbfs nodes required by SDL's libusb HID backend. */
    glob_t devices = {0};
    static unsigned previous;
    unsigned fingerprint = 2166136261u, count = 0;
    struct { unsigned bus, address, vendor, product; } inventory[64];
    if (glob("/sys/bus/usb/devices/*", 0, NULL, &devices)) { globfree(&devices); return; }
    (void)mkdir("/dev/bus", 0755); (void)mkdir("/dev/bus/usb", 0755);
    for (size_t i = 0; i < devices.gl_pathc; ++i) {
        unsigned bus, device;
        char path[512];
        if (!read_unsigned(devices.gl_pathv[i], "busnum", &bus) ||
            !read_unsigned(devices.gl_pathv[i], "devnum", &device) || !bus || bus > 255 || !device || device > 127) continue;
        unsigned vendor = 0, product = 0;
        snprintf(path, sizeof(path), "%s/idVendor", devices.gl_pathv[i]);
        FILE *identity = fopen(path, "r"); if (identity) { if (fscanf(identity, "%x", &vendor) != 1) vendor = 0; fclose(identity); }
        snprintf(path, sizeof(path), "%s/idProduct", devices.gl_pathv[i]);
        identity = fopen(path, "r"); if (identity) { if (fscanf(identity, "%x", &product) != 1) product = 0; fclose(identity); }
        fingerprint = (fingerprint ^ ((bus << 24) | (device << 16) | vendor)) * 16777619u;
        fingerprint = (fingerprint ^ product) * 16777619u;
        if (count < 64) {
            inventory[count].bus = bus; inventory[count].address = device;
            inventory[count].vendor = vendor; inventory[count++].product = product;
        }
        snprintf(path, sizeof(path), "%s/dev", devices.gl_pathv[i]);
        FILE *file = fopen(path, "r"); unsigned major_number, minor_number; char tail;
        if (!file) continue;
        int valid = fscanf(file, "%u:%u %c", &major_number, &minor_number, &tail) == 2;
        fclose(file);
        if (!valid || major_number != 189 || minor_number != (bus - 1) * 128 + device - 1) continue;
        snprintf(path, sizeof(path), "/dev/bus/usb/%03u", bus); (void)mkdir(path, 0755);
        snprintf(path, sizeof(path), "/dev/bus/usb/%03u/%03u", bus, device);
        struct stat status;
        if (!lstat(path, &status)) {
            if (!S_ISCHR(status.st_mode) || status.st_rdev != makedev(major_number, minor_number))
                fprintf(stderr, "USB_INPUT invalid_node=%s\n", path);
            continue;
        }
        if (errno != ENOENT) continue;
        if (mknod(path, S_IFCHR | 0600, makedev(major_number, minor_number)))
            fprintf(stderr, "USB_INPUT node_failed=%s errno=%d\n", path, errno);
        else fprintf(stderr, "USB_INPUT created_usbfs=%s\n", path);
    }
    if (fingerprint != previous) {
        previous = fingerprint;
        fprintf(stderr, "USB_INPUT inventory_changed devices=%u\n", count);
        for (unsigned i = 0; i < count; ++i)
            fprintf(stderr, "USB_INPUT usb=%u:%u vendor=%04x product=%04x\n", inventory[i].bus,
                    inventory[i].address, inventory[i].vendor, inventory[i].product);
    }
    globfree(&devices);
}

static volatile sig_atomic_t stopping;
static void stop(int signal) { (void)signal; stopping = 1; }
static const unsigned keys[] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_SELECT,
    BTN_MODE, BTN_START, BTN_THUMBL, BTN_THUMBR, BTN_TL, BTN_TR,
    BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT};
static const unsigned axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
struct pad {
    SDL_GameController *controller;
    SDL_hid_device *raw;
    char path[256];
    int output, seen;
    struct ls_gp_pad previous;
};

static int create_input(const struct ls_gp_pad *pad) {
    const char *shared_only=getenv("LILSMOKE_GAMEPADS_SHARED_ONLY");
    if(shared_only && !strcmp(shared_only,"1"))return -1;
    struct uinput_user_dev device;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 && access("/sys/class/misc/uinput/dev", R_OK) == 0) {
        if (mknod("/dev/uinput", S_IFCHR | 0600, makedev(10, 223)) && errno != EEXIST) return -1;
        fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (fd < 0) return -1;
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "LilSmoke USB %.60s", pad->name);
    device.id.bustype = BUS_USB; device.id.vendor = pad->vendor; device.id.product = pad->product;
    device.id.version = pad->version;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) || ioctl(fd, UI_SET_EVBIT, EV_ABS)) goto fail;
    for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
        if (ioctl(fd, UI_SET_KEYBIT, keys[i])) goto fail;
    for (unsigned i = 0; i < 6; ++i) {
        if (ioctl(fd, UI_SET_ABSBIT, axes[i])) goto fail;
        device.absmin[axes[i]] = i < 4 ? -32768 : 0;
        device.absmax[axes[i]] = 32767; device.absflat[axes[i]] = i < 4 ? 3500 : 0;
    }
    if (write(fd, &device, sizeof(device)) != sizeof(device) || ioctl(fd, UI_DEV_CREATE)) goto fail;
    return fd;
fail:
    close(fd); return -1;
}

static int emit(int fd, const struct ls_gp_pad *current, const struct ls_gp_pad *previous, int first) {
    /* The M8 kernel's input_event is 16 bytes, despite musl's time64 headers. */
#ifdef __mips__
    struct event { int32_t sec, usec; uint16_t type, code; int32_t value; } events[22];
#else
    struct input_event events[22];
#endif
    unsigned count = 0;
    memset(events, 0, sizeof(events));
    for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
        int before = (previous->buttons >> i) & 1, after = (current->buttons >> i) & 1;
        if (first || before != after) {
            events[count].type = EV_KEY; events[count].code = keys[i]; events[count++].value = after;
        }
    }
    for (unsigned i = 0; i < 6; ++i) if (first || previous->axes[i] != current->axes[i]) {
        events[count].type = EV_ABS; events[count].code = axes[i]; events[count++].value = current->axes[i];
    }
    if (!count) return 1;
    events[count].type = EV_SYN; events[count++].code = SYN_REPORT;
    return write(fd, events, count * sizeof(events[0])) == (ssize_t)(count * sizeof(events[0]));
}
static void disconnect(struct pad *pad) {
    if (pad->output >= 0) { ioctl(pad->output, UI_DEV_DESTROY); close(pad->output); }
    if (pad->controller) SDL_GameControllerClose(pad->controller);
    if (pad->raw) SDL_hid_close(pad->raw);
    memset(pad, 0, sizeof(*pad)); pad->output = -1;
}

static void scan_raw(struct pad *pads) {
    static uint32_t instance = 0x70700000;
    struct SDL_hid_device_info *list = SDL_hid_enumerate(0x0079, 0x181c);
    for (unsigned slot = 0; slot < LS_GP_SLOTS; ++slot) pads[slot].seen = 0;
    for (const struct SDL_hid_device_info *info = list; info; info = info->next) {
        /* The captured RK707 gamepad is interface 0. Never claim keyboard or
           mouse interfaces, or open the first receiver by VID/PID repeatedly. */
        if (!info->path || strlen(info->path) >= sizeof(pads[0].path) || info->interface_number != 0 ||
            (info->usage_page && (info->usage_page != 1 || (info->usage != 4 && info->usage != 5)))) continue;
        unsigned slot;
        for (slot = 0; slot < LS_GP_SLOTS; ++slot)
            if (pads[slot].raw && !strcmp(pads[slot].path, info->path)) break;
        if (slot < LS_GP_SLOTS) { pads[slot].seen = 1; continue; }
        for (slot = 0; slot < LS_GP_SLOTS; ++slot)
            if (!pads[slot].controller && !pads[slot].raw) break;
        if (slot == LS_GP_SLOTS) break;
        struct pad *pad = &pads[slot];
        pad->raw = SDL_hid_open_path(info->path, 0);
        if (!pad->raw) {
            fprintf(stderr, "USB_INPUT rk707=open_failed path=%s error=%s\n", info->path, SDL_GetError());
            continue;
        }
        if (SDL_hid_set_nonblocking(pad->raw, 1) < 0) { disconnect(pad); continue; }
        snprintf(pad->path, sizeof(pad->path), "%s", info->path); pad->seen = 1;
        memset(&pad->previous, 0, sizeof(pad->previous));
        pad->previous.instance = ++instance;
        pad->previous.vendor = 0x0079; pad->previous.product = 0x181c;
        pad->previous.version = info->release_number;
        snprintf(pad->previous.name, sizeof(pad->previous.name), "Rii RK707 PS Input");
        fprintf(stderr, "USB_INPUT rk707=opened slot=%u path=%s instance=%u backend=shared-state\n",
                slot, pad->path, pad->previous.instance);
    }
    SDL_hid_free_enumeration(list);
    for (unsigned slot = 0; slot < LS_GP_SLOTS; ++slot) if (pads[slot].raw && !pads[slot].seen) {
        fprintf(stderr, "USB_INPUT rk707=disconnected slot=%u reason=removed\n", slot);
        disconnect(&pads[slot]);
    }
}

static void poll_raw(struct pad *pad, struct ls_gp_pad *data, unsigned slot) {
    unsigned char report[128];
    for (unsigned budget = 0; budget < 16; ++budget) {
        int count = SDL_hid_read_timeout(pad->raw, report, sizeof(report), 0);
        if (count < 0) {
            fprintf(stderr, "USB_INPUT rk707=disconnected slot=%u path=%s reason=read error=%s\n",
                    slot, pad->path, SDL_GetError());
            disconnect(pad); return;
        }
        if (!count) break;
        if (rk707_decode(report, (size_t)count, &pad->previous)) {
            if (!pad->previous.connected)
                fprintf(stderr, "USB_INPUT rk707=connected slot=%u validated_report=%d\n", slot, count);
            pad->previous.connected = 1;
        }
    }
    /* HID devices may report only changes. Inventory/read errors determine
       removal; a healthy idle/held pad must not expire after one second. */
    *data = pad->previous;
}

int main(int argc, char **argv) {
    const int fixture = argc == 2 && !strcmp(argv[1], "--fixture");
    const int rk_fixture = argc == 2 && !strcmp(argv[1], "--rk707-fixture");
    const char *hotplug_env = getenv("LILSMOKE_GAMEPAD_FIXTURE_HOTPLUG");
    const int fixture_hotplug = (fixture || rk_fixture) && hotplug_env && !strcmp(hotplug_env, "1");
    if (argc > 1 && !fixture && !rk_fixture) { fprintf(stderr, "usage: lilsmoke-usb-gamepads [--fixture|--rk707-fixture]\n"); return 2; }
    const char *path = getenv("LILSMOKE_GAMEPAD_STATE");
    if (!path) path = "/run/lilsmoke-gamepads.state";
    char stage[512]; int fd; struct ls_gp_state *shared;
    if (ls_owner_acquire(&state_owner, path)) {
        fprintf(stderr, "USB_INPUT state_owner_busy path=%s errno=%d\n", path, errno);
        return 1;
    }
    if (atexit(release_state_owner)) { release_state_owner(); return 1; }
    fprintf(stderr, "USB_INPUT state_owner=mkdir pid=%ld record=%s\n", (long)getpid(), state_owner.record);
    if (snprintf(stage, sizeof(stage), "%s.new.%ld", state_owner.state, (long)getpid()) >= (int)sizeof(stage)) return 2;
    fd = openat(state_owner.parent, stage, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0 || ftruncate(fd, sizeof(*shared))) { perror("gamepad state"); if(fd>=0)close(fd); return 1; }
    shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) { close(fd); unlinkat(state_owner.parent, stage, 0); return 1; }
    memset(shared, 0, sizeof(*shared)); shared->magic=LS_GP_MAGIC;shared->abi=1;shared->pid=getpid();
    if (renameat(state_owner.parent, stage, state_owner.parent, state_owner.state)) { perror("gamepad state publish"); munmap(shared,sizeof(*shared));close(fd);unlinkat(state_owner.parent,stage,0);return 1; }
    signal(SIGTERM, stop); signal(SIGINT, stop);
    if (!fixture && !rk_fixture) prepare_usb_nodes();
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint("LILSMOKE_HIDAPI_POLL", "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER)) { fprintf(stderr, "USB_INPUT SDL=%s\n",SDL_GetError()); return 1; }
    SDL_GameControllerEventState(SDL_DISABLE);
    SDL_JoystickEventState(SDL_DISABLE);
    struct pad pads[LS_GP_SLOTS];
    memset(pads,0,sizeof(pads));for(unsigned i=0;i<LS_GP_SLOTS;++i)pads[i].output=-1;
    SDL_Joystick *virtual_pad=NULL; int virtual_index = -1;
    if (fixture) {
        virtual_index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
        if(virtual_index<0 || !(virtual_pad=SDL_JoystickOpen(virtual_index))) return 3;
    }
    fprintf(stderr,"USB_INPUT start pid=%ld state=%s fixture=%d\n",(long)getpid(),path,fixture);
    struct ls_gp_pad rk_state = {0};
    if (rk_fixture) {
        rk_state.connected = 1; rk_state.instance = 0x70700001;
        rk_state.vendor = 0x0079; rk_state.product = 0x181c;
        snprintf(rk_state.name, sizeof(rk_state.name), "Rii RK707 PS Input Fixture");
    }
    uint64_t next_scan=0;unsigned ticks=0;
    while(!stopping) {
        if (fixture_hotplug && fixture && ticks == 100) {
            SDL_JoystickClose(virtual_pad); virtual_pad = NULL;
            if (SDL_JoystickDetachVirtual(virtual_index)) return 3;
        }
        if (fixture_hotplug && fixture && ticks == 180) {
            virtual_index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
            if (virtual_index < 0 || !(virtual_pad = SDL_JoystickOpen(virtual_index))) return 3;
        }
        SDL_PumpEvents();SDL_GameControllerUpdate();
        uint64_t now=ls_gp_now();
        for(unsigned slot=0;slot<LS_GP_SLOTS;++slot)
            if(pads[slot].controller && !SDL_GameControllerGetAttached(pads[slot].controller)) {
                fprintf(stderr,"USB_INPUT disconnected slot=%u\n",slot); disconnect(&pads[slot]);
            }
        if(now>=next_scan) {
            next_scan=now+250;
            if (!fixture && !rk_fixture) prepare_usb_nodes();
            if (!fixture && !rk_fixture) scan_raw(pads);
            for(int index=0;index<SDL_NumJoysticks();++index) {
                SDL_JoystickGUID guid=SDL_JoystickGetDeviceGUID(index);
                /* Only HIDAPI devices: native evdev/D-Input remains untouched. */
                if(!fixture && guid.data[14]!='h') continue;
                if(!SDL_IsGameController(index)) continue;
                SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(index);
                unsigned slot;int known=0;
                for(slot=0;slot<LS_GP_SLOTS;++slot) if(pads[slot].controller &&
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[slot].controller))==id) known=1;
                if(known)continue;
                for(slot=0;slot<LS_GP_SLOTS && (pads[slot].controller || pads[slot].raw);++slot){}
                if(slot==LS_GP_SLOTS)break;
                pads[slot].controller=SDL_GameControllerOpen(index);
                if(!pads[slot].controller){fprintf(stderr,"USB_INPUT open_failed=%s\n",SDL_GetError());continue;}
                memset(&pads[slot].previous,0,sizeof(pads[slot].previous));
                fprintf(stderr,"USB_INPUT connected slot=%u name=%s hidapi=%d\n",slot,SDL_GameControllerName(pads[slot].controller),guid.data[14]=='h');
            }
        }
        if(virtual_pad) {
            SDL_JoystickSetVirtualAxis(virtual_pad,SDL_CONTROLLER_AXIS_LEFTX,16384);
            SDL_JoystickSetVirtualAxis(virtual_pad,SDL_CONTROLLER_AXIS_RIGHTY,-32768);
            SDL_JoystickSetVirtualAxis(virtual_pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);
            SDL_JoystickSetVirtualButton(virtual_pad,SDL_CONTROLLER_BUTTON_A,(ticks/20)%2);
            SDL_JoystickSetVirtualButton(virtual_pad,SDL_CONTROLLER_BUTTON_X,1);
            SDL_GameControllerUpdate();
        }
        struct ls_gp_state state;memset(&state,0,sizeof(state));state.millis=now;
        if (rk_fixture && (!fixture_hotplug || ticks < 100 || ticks >= 180)) {
            if (fixture_hotplug && ticks >= 180) rk_state.instance = 0x70700002;
            unsigned char report[27] = {0x48,0,15,191,128,128,0};
            rk707_decode(report, sizeof(report), &rk_state);
            state.pads[0] = rk_state;
        }
        for(unsigned slot=0;slot<LS_GP_SLOTS;++slot) {
            struct pad *pad=&pads[slot];struct ls_gp_pad *data=&state.pads[slot];
            if (pad->raw) { poll_raw(pad, data, slot); continue; }
            if(!pad->controller || !SDL_GameControllerGetAttached(pad->controller))continue;
            SDL_Joystick *joystick=SDL_GameControllerGetJoystick(pad->controller);
            data->connected=1;data->instance=SDL_JoystickInstanceID(joystick);
            data->vendor=SDL_GameControllerGetVendor(pad->controller);data->product=SDL_GameControllerGetProduct(pad->controller);
            data->version=SDL_GameControllerGetProductVersion(pad->controller);
            const char *name=SDL_GameControllerName(pad->controller);
            snprintf(data->name,sizeof(data->name),"%s",name?name:"HIDAPI Gamepad");
            for(unsigned i=0;i<15;++i) if(SDL_GameControllerGetButton(pad->controller,(SDL_GameControllerButton)i))data->buttons|=1U<<i;
            for(unsigned i=0;i<6;++i)data->axes[i]=SDL_GameControllerGetAxis(pad->controller,(SDL_GameControllerAxis)i);
            if(!pad->previous.connected && !fixture) {
                pad->output=create_input(data);
                fprintf(stderr,"USB_INPUT backend=%s slot=%u vendor=%04x product=%04x\n",
                        pad->output>=0?"uinput":"shared-state",slot,data->vendor,data->product);
            }
            if(pad->output>=0 && !emit(pad->output,data,&pad->previous,!pad->previous.connected)) {
                ioctl(pad->output,UI_DEV_DESTROY);close(pad->output);pad->output=-1;
                fprintf(stderr,"USB_INPUT uinput_lost slot=%u errno=%d; shared-state fallback\n",slot,errno);
            }
            data->exported=pad->output>=0;pad->previous=*data;
            if(fixture && ticks%60==0) fprintf(stderr,"Fixture buttons=%08x axes=%d,%d,%d,%d,%d,%d\n",data->buttons,data->axes[0],data->axes[1],data->axes[2],data->axes[3],data->axes[4],data->axes[5]);
        }
        __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
        shared->millis=state.millis;memcpy(shared->pads,state.pads,sizeof(state.pads));
        __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
        ++ticks;SDL_Delay(8);
        if((fixture || rk_fixture) && ticks>=300)stopping=1;
    }
    __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);memset(shared->pads,0,sizeof(shared->pads));
    __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<LS_GP_SLOTS;++i)disconnect(&pads[i]);
    if(virtual_pad)SDL_JoystickClose(virtual_pad);
    SDL_Quit();munmap(shared,sizeof(*shared));close(fd);release_state_owner();return 0;
}
