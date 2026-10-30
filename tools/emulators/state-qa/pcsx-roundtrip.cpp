#include "state-store.h"
#include "r3000a.h"
#include "plugins.h"
#include "plugin_lib.h"
#include "spu/spu_pcsxrearmed/spu_config.h"
#include <cassert>
#include <cstring>
#include <fstream>

static uint16_t pixels[320 * 240];

int main(int argc, char **argv) {
    assert(argc == 2);
    setenv("LILSMOKE_STATE_ROOT", argv[1], 1);
    unsetenv("LILSMOKE_STATE_CONTENT");
    memset(&Config, 0, sizeof Config);
    Config.Cpu = CPU_INTERPRETER;
    Config.HLE = 1;
    Config.PsxAuto = 1;
    Config.SpuUpdateFreq = SPU_UPDATE_FREQ_16;
    snprintf(Config.Mcd1, sizeof Config.Mcd1, "%s/card1.mcr", argv[1]);
    snprintf(Config.Mcd2, sizeof Config.Mcd2, "%s/card2.mcr", argv[1]);
    spu_config.iHaveConfiguration = 1;
    spu_config.iVolume = 1024;
    spu_config.iUseFixedUpdates = 1;
    spu_config.iTempo = 1;
    rkgame_pcsx_set_video_buffer(pixels, 320, 240, 320);
    assert(psxInit() == 0);
    assert(LoadPlugins() == 0);
    pl_init();
    psxReset();
    PcsxStateStore states, other;
    states.init("/roms/one/Game.cue");
    other.init("/roms/two/Game.cue");
    assert(states.path(0) != other.path(0));
    std::string message;
    assert(!states.load(9, message));
    psxM[0x1200] = 0x42;
    psxH[0x200] = 0x37;
    psxRegs.GPR.r[8] = 0x12345678;
    psxRegs.pc = 0x80010000;
    assert(states.save(0, message));
    const auto first = states.path(0);
    psxM[0x1200] = 0x99;
    psxH[0x200] = 0x88;
    psxRegs.GPR.r[8] = 0x87654321;
    psxRegs.pc = 0x80020000;
    assert(states.save(1, message));
    assert(states.load(0, message));
    assert(psxM[0x1200] == 0x42 && psxH[0x200] == 0x37);
    assert(psxRegs.GPR.r[8] == 0x12345678 && psxRegs.pc == 0x80010000);
    assert(states.load(1, message));
    assert(static_cast<unsigned char>(psxM[0x1200]) == 0x99 && static_cast<unsigned char>(psxH[0x200]) == 0x88);
    assert(psxRegs.GPR.r[8] == 0x87654321 && psxRegs.pc == 0x80020000);
    PcsxStateStore restarted;
    restarted.init("/roms/one/Game.cue");
    assert(restarted.path(0) == first && restarted.load(0, message));
    assert(psxM[0x1200] == 0x42);
    std::filesystem::copy_file(first, states.path(2), std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(states.path(2), 64);
    assert(!states.load(2, message));
    assert(psxM[0x1200] == 0x42 && psxRegs.GPR.r[8] == 0x12345678);
    setenv("LILSMOKE_STATE_ROOT", "/proc/no-state-writes", 1);
    PcsxStateStore unwritable; unwritable.init("/roms/one/Game.cue");
    assert(!unwritable.save(0, message));
    assert(states.load(0, message));
    std::puts("PCSX_REAL_STATE_ROUNDTRIP_PASS ram=restored scratchpad=restored cpu=restored slots=2 corrupt=rejected missing=rejected storage_error=reported same_basename=isolated");
    std::fflush(nullptr);
    // Avoid unrelated SDL/musl teardown, just as the native menu worker does.
    _exit(0);
}
