#include "state-store.h"
#include "r3000a.h"
#include "plugins.h"
#include "misc.h"
#include "plugin_lib.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstddef>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <vector>
#include <fstream>

extern "C" int pcsx_load_game(const char **);
static uint16_t pixels[320 * 240];
static unsigned frames;
static unsigned presented;
static std::string scenario;
static PcsxStateStore states;
static std::vector<char> saved;
static uint32_t savedPc, savedReg;
static size_t failAllocation;

extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
    if (failAllocation && size == failAllocation) {
        errno = ENOMEM;
        return nullptr;
    }
    return __real_malloc(size);
}

static std::string contents(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static void shortenedState(const std::filesystem::path &from, const std::filesystem::path &to) {
    gzFile input = gzopen(from.c_str(), "rb");
    assert(input);
    std::vector<char> raw;
    char buffer[8192]; int count;
    while ((count = gzread(input, buffer, sizeof buffer)) > 0) raw.insert(raw.end(), buffer, buffer + count);
    assert(count == 0 && gzclose(input) == Z_OK && raw.size() > 3 * 1024 * 1024 + 16);
    raw.resize(raw.size() - 16);
    gzFile output = gzopen(to.c_str(), "wb4");
    assert(output && gzwrite(output, raw.data(), raw.size()) == static_cast<int>(raw.size()));
    assert(gzclose(output) == Z_OK);
}
static PcsxSaveFuncs originalIo;
static int traceWrite(void *f, const void *data, u32 len) {
    errno = 0;
    const int n = originalIo.write(f, data, len);
    const int error = errno;
    int zerror;
    const char *detail = gzerror(static_cast<gzFile>(f), &zerror);
    std::fprintf(stderr, "PCSX_RECOVERY_WRITE requested=%u result=%d errno=%d zlib=%d detail=%s\n", len, n, error, zerror, detail);
    errno = error;
    return n;
}
static int traceClose(void *f) {
    errno = 0;
    const int n = originalIo.close(f);
    const int error = errno;
    std::fprintf(stderr, "PCSX_RECOVERY_CLOSE result=%d errno=%d\n", n, error);
    errno = error;
    return n;
}

static void denySyscall(int number, int error) {
    sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(number), 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<unsigned>(error)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    sock_fprog filter = {4, instructions};
    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
    assert(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter) == 0);
    std::fprintf(stderr, "PCSX_RECOVERY_KERNEL_FILTER syscall=%d errno=%d\n", number, error);
}

static void diskFull() {
    sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 3, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSPC),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    sock_fprog filter = {6, code};
    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
    assert(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter) == 0);
    std::fprintf(stderr, "PCSX_RECOVERY_KERNEL_FILTER syscall=write fd>=3 errno=%d\n", ENOSPC);
}

static void finish() {
    rusage usage{}; assert(getrusage(RUSAGE_SELF, &usage) == 0);
    std::fprintf(stderr, "PCSX_RECOVERY_MEMORY maxrss_kib=%ld data_limit_mib=80\n", usage.ru_maxrss);
    std::fprintf(stderr, "PCSX_RECOVERY_PASS scenario=%s updates=%u presented=%u pc=%08x disc=%s cpu=%d ram=%p\n",
                 scenario.c_str(), frames, presented, psxRegs.pc, CdromId, Config.Cpu, psxM);
    std::fflush(nullptr);
    _exit(0);
}

static void step() {
    ++frames;
    if (frames == 90) {
        assert(CdromId[0] && psxRegs.pc != 0);
        assert(Config.Cpu == CPU_DYNAREC && reinterpret_cast<uintptr_t>(psxM) == 0);
        if (scenario.find("enosys") != std::string::npos) denySyscall(SYS_fsync, ENOSYS);
        if (scenario == "all-sync-enosys") {
            denySyscall(SYS_fdatasync, ENOSYS);
            denySyscall(SYS_syncfs, ENOSYS);
        }
        if (scenario.find("baseline") == 0) {
            originalIo = SaveFuncs;
            SaveFuncs.write = traceWrite;
            SaveFuncs.close = traceClose;
            errno = 0;
            const std::string name = "/bench/" + scenario + ".state";
            const int result = SaveState(name.c_str());
            const int error = errno;
            std::fprintf(stderr, "PCSX_RECOVERY_BASELINE result=%d errno=%d error=%s\n", result, error, strerror(error));
            assert(result == -1);
            finish();
        }
        std::string message;
        if (scenario == "all-sync-enosys") {
            assert(!states.save(0, message));
            assert(!states.exists(0));
            finish();
        }
        assert(states.save(0, message));
        std::ofstream(states.path(0).parent_path() / ".saving.state") << "legacy-save-preserved";
        std::ofstream(states.path(0).parent_path() / ".recovery.state") << "legacy-recovery-preserved";
        saved.assign(psxM, psxM + 0x200000);
        savedPc = psxRegs.pc;
        savedReg = psxRegs.GPR.r[8];
    }
    if (frames == 180) {
        assert(saved.size() == 0x200000);
        assert(std::memcmp(saved.data(), psxM, saved.size()) != 0);
        std::string message;
        assert(states.save(1, message));
        assert(states.load(0, message));
        assert(std::memcmp(saved.data(), psxM, saved.size()) == 0);
        assert(psxRegs.pc == savedPc && psxRegs.GPR.r[8] == savedReg);
        const auto old = states.path(0);
        std::filesystem::copy_file(old, states.path(2));
        std::filesystem::resize_file(states.path(2), 64);
        assert(!states.load(2, message));
        assert(std::memcmp(saved.data(), psxM, saved.size()) == 0);
        assert(!states.load(9, message));
        assert(states.load(1, message));
        assert(std::memcmp(saved.data(), psxM, saved.size()) != 0);
        const std::vector<char> current(psxM, psxM + 0x200000);
        const auto currentPc = psxRegs.pc;
        shortenedState(states.path(0), states.path(3));
        assert(!states.load(3, message));
        assert(message == "State incompatible; game restored");
        assert(std::memcmp(current.data(), psxM, current.size()) == 0 && psxRegs.pc == currentPc);
        assert(states.load(0, message));
        assert(std::memcmp(saved.data(), psxM, saved.size()) == 0);
        assert(contents(old.parent_path() / ".saving.state") == "legacy-save-preserved");
        assert(contents(old.parent_path() / ".recovery.state") == "legacy-recovery-preserved");
        if (scenario == "io-error" || scenario == "rename-error" || scenario == "enospc" || scenario == "gpu-oom" || scenario == "screenshot-oom") {
            const auto original = contents(old);
            if (scenario == "io-error") denySyscall(SYS_fsync, EIO);
            if (scenario == "rename-error") denySyscall(SYS_rename, EACCES);
            if (scenario == "enospc") diskFull();
            if (scenario == "gpu-oom") failAllocation = sizeof(GPUFreeze_t);
            if (scenario == "screenshot-oom") failAllocation = 160 * 120 * 2;
            assert(!states.save(0, message));
            failAllocation = 0;
            assert(contents(old) == original);
            // Validation/read-only load must not fsync the input descriptor.
            assert(CheckState(old.c_str(), nullptr, false, nullptr) == 0);
            finish();
        }
    }
    if (frames == 240) {
        assert(std::memcmp(saved.data(), psxM, saved.size()) != 0);
        finish();
    }
}

int main(int argc, char **argv) {
    assert(argc == 4);
    scenario = argv[3];
    rlimit limit = {80 * 1024 * 1024, 80 * 1024 * 1024};
    assert(setrlimit(RLIMIT_DATA, &limit) == 0);
    setenv("LILSMOKE_STATE_ROOT", argv[2], 1);
    states.init(argv[1]);
    rkgame_pcsx_set_work_path("/persist/");
    rkgame_pcsx_set_video_buffer(pixels, 320, 240, 320);
    rkgame_pcsx_set_video_callback([](const uint16_t *, int, int, int) { ++presented; });
    // No host keyboard or physical input devices are opened.
    rkgame_pcsx_set_input_callback([](int port) -> unsigned short { if (!port) step(); return 0xffff; });
    denySyscall(SYS_socketpair, ENOSYS);
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1 && errno == ENOSYS);
    const char *disc = argv[1];
    pcsx_load_game(&disc);
    return 1;
}
