#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <strings.h>

#include "port.h"
#include "r3000a.h"
#include "plugins.h"
#include "plugin_lib.h"
#include "misc.h"

#ifdef SPU_PCSXREARMED
#include "spu/spu_pcsxrearmed/spu_config.h"
#endif

#ifdef GPU_UNAI
#include "gpu/gpu_unai/gpu.h"
#endif

#ifdef RKGAME_USE_SDL_JOYSTICK
#include <SDL.h>
#endif

#ifdef PSXREC
extern u32 cycle_multiplier;
#endif

#ifndef RKGAME_DEFAULT_WORK_PATH
#define RKGAME_DEFAULT_WORK_PATH "/mnt/sdcard/cubegm/"
#endif

#ifndef RKGAME_FRAME_WIDTH
#define RKGAME_FRAME_WIDTH 320
#endif

#ifndef RKGAME_FRAME_HEIGHT
#define RKGAME_FRAME_HEIGHT 240
#endif

#if defined(__GNUC__)
#define RKGAME_WEAK __attribute__((weak))
#else
#define RKGAME_WEAK
#endif

extern "C" void wise_mem_init(void) RKGAME_WEAK;
extern "C" void wise_mem_deinit(void) RKGAME_WEAK;
extern "C" void InitKeyMapping0fEmuType(void) RKGAME_WEAK;
extern "C" void RARCH_LOG(const char *fmt, ...) RKGAME_WEAK;

extern "C" char work_path[PATH_MAX] RKGAME_WEAK = RKGAME_DEFAULT_WORK_PATH;

unsigned short *SCREEN;

static char homedir[PATH_MAX] = "./.pcsx4all";
static char memcardsdir[PATH_MAX] = "./.pcsx4all/memcards";
static char biosdir[PATH_MAX] = "./.pcsx4all/bios";
static char patchesdir[PATH_MAX] = "./.pcsx4all/patches";
char sstatesdir[PATH_MAX] = "./.pcsx4all/sstates";

static uint16_t fallback_screen[RKGAME_FRAME_WIDTH * RKGAME_FRAME_HEIGHT];
static int frame_width = RKGAME_FRAME_WIDTH;
static int frame_height = RKGAME_FRAME_HEIGHT;
static int frame_pitch = RKGAME_FRAME_WIDTH;
static bool pcsx4all_initted = false;
static bool emu_running = false;
static unsigned short pad_state[2] = { 0xffff, 0xffff };
static unsigned char analog_arrow = 0;
static unsigned char analog_mode = 0;
static unsigned char rkgame_special_fix = 0;
static const char *pcsx_game_info;
static rkgame_pcsx_video_cb video_cb;
static rkgame_pcsx_input_cb input_cb;
static rkgame_pcsx_log_cb log_cb;

enum {
	DKEY_SELECT = 0,
	DKEY_L3,
	DKEY_R3,
	DKEY_START,
	DKEY_UP,
	DKEY_RIGHT,
	DKEY_DOWN,
	DKEY_LEFT,
	DKEY_L2,
	DKEY_R2,
	DKEY_L1,
	DKEY_R1,
	DKEY_TRIANGLE,
	DKEY_CIRCLE,
	DKEY_CROSS,
	DKEY_SQUARE,
};

#ifdef RKGAME_USE_SDL_JOYSTICK
static SDL_Joystick *joysticks[2];
#endif

static void rkgame_log(const char *fmt, ...)
{
	char msg[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if (log_cb) {
		log_cb(msg);
		return;
	}

	if (RARCH_LOG) {
		RARCH_LOG("%s", msg);
		return;
	}

	fputs(msg, stdout);
}

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
	if (!dst || dst_size == 0)
		return;
	if (!src)
		src = "";
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}

static void path_join(char *dst, size_t dst_size, const char *base, const char *leaf)
{
	if (!base || base[0] == '\0') {
		snprintf(dst, dst_size, "%s", leaf ? leaf : "");
		return;
	}

	size_t len = strlen(base);
	if (len > 0 && (base[len - 1] == '/' || base[len - 1] == '\\'))
		snprintf(dst, dst_size, "%s%s", base, leaf ? leaf : "");
	else
		snprintf(dst, dst_size, "%s/%s", base, leaf ? leaf : "");
}

static void make_dir(const char *path)
{
	if (!path || path[0] == '\0')
		return;
	if (mkdir(path, 0777) < 0 && errno != EEXIST)
		rkgame_log("mkdir failed for %s: %s\n", path, strerror(errno));
}

static void setup_paths(void)
{
    safe_copy(homedir, sizeof(homedir), "/persist/config/pcsx4all");
    safe_copy(sstatesdir, sizeof(sstatesdir), "/persist/states/pcsx4all");
    safe_copy(memcardsdir, sizeof(memcardsdir), "/persist/saves/pcsx4all");
    safe_copy(biosdir, sizeof(biosdir), "/persist/bios");
    safe_copy(patchesdir, sizeof(patchesdir), "/persist/config/pcsx4all/patches");
    make_dir(homedir);
    make_dir(sstatesdir);
    make_dir(memcardsdir);
    make_dir(biosdir);
    make_dir(patchesdir);
}

static void probe_lastdir(void)
{
	if (!Config.LastDir[0])
		return;

	DIR *dir = opendir(Config.LastDir);
	if (!dir) {
		safe_copy(Config.LastDir, sizeof(Config.LastDir), homedir);
	} else {
		closedir(dir);
	}
}

static void set_default_config(void)
{
	memset(&Config, 0, sizeof(Config));

	path_join(Config.Mcd1, sizeof(Config.Mcd1), memcardsdir, "mcd001.mcr");
	path_join(Config.Mcd2, sizeof(Config.Mcd2), memcardsdir, "mcd002.mcr");
	safe_copy(Config.PatchesDir, sizeof(Config.PatchesDir), patchesdir);
	safe_copy(Config.BiosDir, sizeof(Config.BiosDir), biosdir);
	safe_copy(Config.Bios, sizeof(Config.Bios), "scph1001.bin");

	Config.Xa = 0;
	Config.Mdec = 0;
	Config.PsxAuto = 1;
	Config.Cdda = 0;
	Config.HLE = 1;
    struct stat bios_stat;
    char bios_path[PATH_MAX];
    path_join(bios_path, sizeof(bios_path), biosdir, "scph1001.bin");
    if (stat(bios_path, &bios_stat) == 0 && S_ISREG(bios_stat.st_mode) &&
        bios_stat.st_size == 524288) Config.HLE = 0;
	Config.SlowBoot = 0;
	Config.RCntFix = 0;
	Config.VSyncWA = 0;
#if defined(PSXREC)
	Config.Cpu = CPU_DYNAREC;
#else
	Config.Cpu = CPU_INTERPRETER;
#endif
	Config.PsxType = PSX_TYPE_NTSC;
	Config.SpuIrq = 0;
	Config.SyncAudio = 0;
	Config.SpuUpdateFreq = SPU_UPDATE_FREQ_16;
	Config.ForcedXAUpdates = FORCED_XA_UPDATES_AUTO;
	Config.ShowFps = 1;
	Config.FrameLimit = 1;
	Config.FrameSkip = 0;
	analog_arrow = 0;
	analog_mode = 0;
	rkgame_special_fix = 0;
	safe_copy(Config.LastDir, sizeof(Config.LastDir), homedir);

#ifdef SPU_PCSXREARMED
	spu_config.iHaveConfiguration = 1;
	spu_config.iVolume = 1024;
	spu_config.iXAPitch = 0;
	spu_config.iUseReverb = 0;
	spu_config.iUseInterpolation = 0;
	spu_config.iTempo = 1;
	spu_config.iUseThread = 0;
	spu_config.iUseFixedUpdates = 1;
	spu_config.iDisabled = 0;
#endif

#ifdef PSXREC
	cycle_multiplier = 0x100;
#endif

#ifdef GPU_UNAI
	gpu_unai_config_ext.ilace_force = 0;
	gpu_unai_config_ext.pixel_skip = 0;
	gpu_unai_config_ext.lighting = 1;
	gpu_unai_config_ext.fast_lighting = 1;
	gpu_unai_config_ext.blending = 1;
	gpu_unai_config_ext.dithering = 0;
#ifndef USE_GPULIB
	gpu_unai_config_ext.prog_ilace = 0;
	gpu_unai_config_ext.frameskip_count = 0;
#endif
#endif
}

void config_load(void)
{
	char config[PATH_MAX];
	char line[MAXPATHLEN + 32];
	int line_num = 0;

	path_join(config, sizeof(config), homedir, "pcsx4all.cfg");
	FILE *f = fopen(config, "r");
	if (!f) {
		rkgame_log("Failed to open config file: \"%s\" for reading.\n", config);
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		char *arg = strchr(line, ' ');
		int value = 0;
		++line_num;

		if (!arg)
			continue;

		*arg++ = '\0';

		if (line_num == 1) {
			if (strcmp(line, "CONFIG_VERSION") != 0) {
				rkgame_log("Incompatible config format for \"%s\".Ignoring.\n", config);
				break;
			}
			sscanf(arg, "%d", &value);
			if (value != CONFIG_VERSION) {
				rkgame_log("Incompatible config version for \"%s\".Required: %d. Found: %d. Ignoring.\n",
				           config, CONFIG_VERSION, value);
				break;
			}
			continue;
		}

		if (!strcmp(line, "Xa")) {
			sscanf(arg, "%d", &value);
			Config.Xa = value;
		} else if (!strcmp(line, "Mdec")) {
			sscanf(arg, "%d", &value);
			Config.Mdec = value;
		} else if (!strcmp(line, "PsxAuto")) {
			sscanf(arg, "%d", &value);
			Config.PsxAuto = value;
		} else if (!strcmp(line, "Cdda")) {
			sscanf(arg, "%d", &value);
			Config.Cdda = value;
		} else if (!strcmp(line, "HLE")) {
			sscanf(arg, "%d", &value);
			Config.HLE = value;
		} else if (!strcmp(line, "RCntFix")) {
			sscanf(arg, "%d", &value);
			Config.RCntFix = value;
		} else if (!strcmp(line, "VSyncWA")) {
			sscanf(arg, "%d", &value);
			Config.VSyncWA = value;
		} else if (!strcmp(line, "Cpu")) {
			sscanf(arg, "%d", &value);
			Config.Cpu = value;
		} else if (!strcmp(line, "PsxType")) {
			sscanf(arg, "%d", &value);
			Config.PsxType = value;
		} else if (!strcmp(line, "SpuIrq")) {
			sscanf(arg, "%d", &value);
			Config.SpuIrq = value;
		} else if (!strcmp(line, "SyncAudio")) {
			sscanf(arg, "%d", &value);
			Config.SyncAudio = value;
		} else if (!strcmp(line, "SpuUpdateFreq")) {
			sscanf(arg, "%d", &value);
			Config.SpuUpdateFreq = (value < SPU_UPDATE_FREQ_MIN || value > SPU_UPDATE_FREQ_MAX)
				? SPU_UPDATE_FREQ_DEFAULT : value;
		} else if (!strcmp(line, "ForcedXAUpdates")) {
			sscanf(arg, "%d", &value);
			Config.ForcedXAUpdates = (value < FORCED_XA_UPDATES_MIN || value > FORCED_XA_UPDATES_MAX)
				? FORCED_XA_UPDATES_DEFAULT : value;
		} else if (!strcmp(line, "ShowFps")) {
			sscanf(arg, "%d", &value);
			Config.ShowFps = value;
		} else if (!strcmp(line, "FrameLimit")) {
			sscanf(arg, "%d", &value);
			Config.FrameLimit = value;
		} else if (!strcmp(line, "FrameSkip")) {
			sscanf(arg, "%d", &value);
			Config.FrameSkip = (value < FRAMESKIP_MIN || value > FRAMESKIP_MAX)
				? FRAMESKIP_OFF : value;
		} else if (!strcmp(line, "AnalogArrow")) {
			sscanf(arg, "%d", &value);
			analog_arrow = value;
		} else if (!strcmp(line, "Analog_Mode")) {
			sscanf(arg, "%d", &value);
			analog_mode = value > 2 ? 0 : value;
#ifdef SPU_PCSXREARMED
		} else if (!strcmp(line, "SpuUseInterpolation")) {
			sscanf(arg, "%d", &value);
			spu_config.iUseInterpolation = value;
		} else if (!strcmp(line, "SpuUseReverb")) {
			sscanf(arg, "%d", &value);
			spu_config.iUseReverb = value;
		} else if (!strcmp(line, "SpuVolume")) {
			sscanf(arg, "%d", &value);
			if (value < 0)
				value = 0;
			if (value > 1024)
				value = 1024;
			spu_config.iVolume = value;
#endif
		} else if (!strcmp(line, "LastDir") ||
		           !strcmp(line, "BiosDir") ||
		           !strcmp(line, "Bios")) {
			size_t len = strlen(arg);
			if (len > 0 && arg[len - 1] == '\n')
				arg[len - 1] = '\0';
			if (strlen(arg) >= MAXPATHLEN)
				continue;
			if (!strcmp(line, "LastDir"))
				safe_copy(Config.LastDir, sizeof(Config.LastDir), arg);
			else if (!strcmp(line, "BiosDir"))
				safe_copy(Config.BiosDir, sizeof(Config.BiosDir), arg);
			else
				safe_copy(Config.Bios, sizeof(Config.Bios), arg);
#ifdef PSXREC
		} else if (!strcmp(line, "CycleMultiplier")) {
			sscanf(arg, "%03x", &value);
			cycle_multiplier = value;
#endif
#ifdef GPU_UNAI
		} else if (!strcmp(line, "clip_368") || !strcmp(line, "pixel_skip")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.pixel_skip = value & 1;
		} else if (!strcmp(line, "lighting")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.lighting = value & 1;
		} else if (!strcmp(line, "fast_lighting")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.fast_lighting = value & 1;
		} else if (!strcmp(line, "blending")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.blending = value & 1;
		} else if (!strcmp(line, "dithering")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.dithering = value & 1;
		} else if (!strcmp(line, "interlace")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.ilace_force = value & 7;
#endif
		}
	}

	fclose(f);
}

void config_save(void)
{
	char config[PATH_MAX];
	path_join(config, sizeof(config), homedir, "pcsx4all.cfg");

	FILE *f = fopen(config, "w");
	if (!f) {
		rkgame_log("Failed to open config file: \"%s\" for writing.\n", config);
		return;
	}

	fprintf(f, "CONFIG_VERSION %d\n"
	           "Xa %d\n"
	           "Mdec %d\n"
	           "PsxAuto %d\n"
	           "Cdda %d\n"
	           "HLE %d\n"
	           "RCntFix %d\n"
	           "VSyncWA %d\n"
	           "Cpu %d\n"
	           "PsxType %d\n"
	           "SpuIrq %d\n"
	           "SyncAudio %d\n"
	           "SpuUpdateFreq %d\n"
	           "ForcedXAUpdates %d\n"
	           "ShowFps %d\n"
	           "FrameLimit %d\n"
	           "FrameSkip %d\n"
	           "AnalogArrow %d\n"
	           "Analog_Mode %d\n",
	        CONFIG_VERSION, Config.Xa, Config.Mdec, Config.PsxAuto,
	        Config.Cdda, Config.HLE, Config.RCntFix, Config.VSyncWA,
	        Config.Cpu, Config.PsxType, Config.SpuIrq, Config.SyncAudio,
	        Config.SpuUpdateFreq, Config.ForcedXAUpdates, Config.ShowFps,
	        Config.FrameLimit, Config.FrameSkip, analog_arrow, analog_mode);

#ifdef SPU_PCSXREARMED
	fprintf(f, "SpuUseInterpolation %d\n", spu_config.iUseInterpolation);
	fprintf(f, "SpuUseReverb %d\n", spu_config.iUseReverb);
	fprintf(f, "SpuVolume %d\n", spu_config.iVolume);
#endif

#ifdef PSXREC
	fprintf(f, "CycleMultiplier %03x\n", cycle_multiplier);
#endif

#ifdef GPU_UNAI
	fprintf(f, "interlace %d\n"
	           "clip_368 %d\n"
	           "lighting %d\n"
	           "fast_lighting %d\n"
	           "blending %d\n"
	           "dithering %d\n",
	        gpu_unai_config_ext.ilace_force,
	        gpu_unai_config_ext.pixel_skip,
	        gpu_unai_config_ext.lighting,
	        gpu_unai_config_ext.fast_lighting,
	        gpu_unai_config_ext.blending,
	        gpu_unai_config_ext.dithering);
#endif

	if (Config.LastDir[0])
		fprintf(f, "LastDir %s\n", Config.LastDir);
	if (Config.BiosDir[0])
		fprintf(f, "BiosDir %s\n", Config.BiosDir);
	if (Config.Bios[0])
		fprintf(f, "Bios %s\n", Config.Bios);

	fclose(f);
}

static void apply_game_specific_fixes(void)
{
#ifdef GPU_UNAI
	if (!strncasecmp(CdromId, "SLPS02124", 9))
		gpu_unai_config_ext.pixel_skip = 1;
#endif

	if (!strncasecmp(CdromId, "SLPS03503", 9))
		Config.VSyncWA = 1;

	if (!strncasecmp(CdromId, "SCES01564", 9) ||
	    !strncasecmp(CdromId, "SCES02031", 9))
		analog_mode = 2;

	if (!strncasecmp(CdromId, "SLES00613", 9) ||
	    !strncasecmp(CdromId, "SLED00690", 9))
		rkgame_special_fix = 1;

	if (!strncasecmp(CdromId, "SCPS45183", 9))
		Config.RCntFix = 1;
}

static void init_input(void)
{
#ifdef RKGAME_USE_SDL_JOYSTICK
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	joysticks[0] = SDL_JoystickOpen(0);
	joysticks[1] = SDL_JoystickOpen(1);
	SDL_JoystickEventState(SDL_ENABLE);
#endif

	pad_state[0] = 0xffff;
	pad_state[1] = 0xffff;
}

static void shutdown_core(void)
{
	config_save();

#ifdef RKGAME_USE_SDL_JOYSTICK
	if (joysticks[0]) {
		SDL_JoystickClose(joysticks[0]);
		joysticks[0] = NULL;
	}
	if (joysticks[1]) {
		SDL_JoystickClose(joysticks[1]);
		joysticks[1] = NULL;
	}
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
#endif

	if (pcsx4all_initted) {
		ReleasePlugins();
		psxShutdown();
		pcsx4all_initted = false;
	}
}

int state_load(int slot)
{
	char savename[PATH_MAX * 2];
	snprintf(savename, sizeof(savename), "%s/%s.%d.sav", sstatesdir, CdromId, slot);
	return FileExists(savename) ? LoadState(savename) : -1;
}

int state_save(int slot)
{
	char savename[PATH_MAX * 2];
	snprintf(savename, sizeof(savename), "%s/%s.%d.sav", sstatesdir, CdromId, slot);
	return SaveState(savename);
}

static void make_pcsx_state_path(char *dst, size_t dst_size, const char *src)
{
	safe_copy(dst, dst_size, src);
	size_t len = strlen(dst);
	if (len >= 3) {
		dst[len - 3] = 'p';
		dst[len - 2] = 's';
	}
}

extern "C" int pcsx_save_state(char *path)
{
	if (!path)
		return 0;

	FILE *f = fopen(path, "wb");
	if (f) {
		uint32_t len = (uint32_t)strlen(path);
		fwrite(&len, 1, sizeof(len), f);
		fwrite(path, 1, len, f);
		fclose(f);
	}

	char pcsx_path[PATH_MAX * 2];
	make_pcsx_state_path(pcsx_path, sizeof(pcsx_path), path);
	return SaveState(pcsx_path) == 0;
}

extern "C" int pcsx_load_state(char *path)
{
	if (!path)
		return 0;

	char pcsx_path[PATH_MAX * 2];
	make_pcsx_state_path(pcsx_path, sizeof(pcsx_path), path);
	return LoadState(pcsx_path) == 0;
}

extern "C" int retro_save_state(char *path)
{
	return pcsx_save_state(path);
}

extern "C" int retro_load_state(char *path)
{
	return pcsx_load_state(path);
}

extern "C" int pcsx_load_game(const char **game_path)
{
	const char *iso = game_path ? *game_path : NULL;
	const char *cdrfilename;

	setup_paths();
	set_default_config();
	config_load();
	probe_lastdir();

	if (iso && iso[0])
		SetIsoFile(iso);

	if (psxInit() == -1) {
		rkgame_log("PSX emulator couldn't be initialized.\n");
		return -1;
	}

	if (LoadPlugins() == -1) {
		rkgame_log("Failed loading plugins.\n");
		psxShutdown();
		return -1;
	}

	pcsx4all_initted = true;
	emu_running = true;
	SCREEN = SCREEN ? SCREEN : fallback_screen;

	pl_init();
	psxReset();

	cdrfilename = GetIsoFile();
	if (cdrfilename[0] != '\0') {
		if (CheckCdrom() == -1) {
			rkgame_log("Failed checking ISO image.\n");
			SetIsoFile(NULL);
		} else {
			rkgame_log("Running ISO image: %s.\n", cdrfilename);
			if (LoadCdrom() == -1) {
				rkgame_log("Failed loading ISO image.\n");
				SetIsoFile(NULL);
			}
		}
	}

	apply_game_specific_fixes();
	init_input();

	if ((GetIsoFile()[0] == '\0') && Config.HLE == 0)
		rkgame_log("Running BIOS.\n");

	if ((GetIsoFile()[0] != '\0') || Config.HLE == 0)
		psxCpu->Execute();

	shutdown_core();
	return 0;
}

extern "C" void *pcsx_Thread(void *unused)
{
	(void)unused;
	pcsx_load_game(&pcsx_game_info);
#ifdef RKGAME_MATCH_THREAD_DETACH
	pthread_detach(pthread_self());
#endif
	return NULL;
}

extern "C" void PCSX_Load(const char *game_path)
{
	pthread_t thread;
	void *thread_ret = NULL;

	if (wise_mem_init)
		wise_mem_init();
	if (InitKeyMapping0fEmuType)
		InitKeyMapping0fEmuType();

	pcsx_game_info = game_path;
	if (pthread_create(&thread, NULL, pcsx_Thread, NULL) == 0) {
		pthread_join(thread, &thread_ret);
		if (wise_mem_deinit)
			wise_mem_deinit();
		return;
	}

	rkgame_log("can't create mui_SoundplayThread process thread \r\n");
}

extern "C" void rkgame_pcsx_set_work_path(const char *path)
{
	if (!path || !path[0])
		return;
	safe_copy(work_path, PATH_MAX, path);
	size_t len = strlen(work_path);
	if (len > 0 && len < PATH_MAX - 1 &&
	    work_path[len - 1] != '/' && work_path[len - 1] != '\\') {
		work_path[len] = '/';
		work_path[len + 1] = '\0';
	}
}

extern "C" void rkgame_pcsx_set_video_buffer(uint16_t *buffer,
                                             int width,
                                             int height,
                                             int pitch_pixels)
{
	if (!buffer || width <= 0 || height <= 0 || pitch_pixels <= 0)
		return;
	SCREEN = buffer;
	frame_width = width;
	frame_height = height;
	frame_pitch = pitch_pixels;
}

extern "C" void rkgame_pcsx_set_video_callback(rkgame_pcsx_video_cb cb)
{
	video_cb = cb;
}

extern "C" void rkgame_pcsx_set_input_callback(rkgame_pcsx_input_cb cb)
{
	input_cb = cb;
}

extern "C" void rkgame_pcsx_set_log_callback(rkgame_pcsx_log_cb cb)
{
	log_cb = cb;
}

void pad_update(void)
{
	if (input_cb) {
		pad_state[0] = input_cb(0);
		pad_state[1] = input_cb(1);
		return;
	}

#ifdef RKGAME_USE_SDL_JOYSTICK
	SDL_JoystickUpdate();
	for (int port = 0; port < 2; ++port) {
		SDL_Joystick *joy = joysticks[port];
		unsigned short state = 0xffff;

		if (!joy) {
			pad_state[port] = state;
			continue;
		}

		if (SDL_JoystickNumAxes(joy) > 0) {
			Sint16 x = SDL_JoystickGetAxis(joy, 0);
			if (x < -16384) state &= ~(1 << DKEY_LEFT);
			if (x >  16384) state &= ~(1 << DKEY_RIGHT);
		}
		if (SDL_JoystickNumAxes(joy) > 1) {
			Sint16 y = SDL_JoystickGetAxis(joy, 1);
			if (y < -16384) state &= ~(1 << DKEY_UP);
			if (y >  16384) state &= ~(1 << DKEY_DOWN);
		}

		struct ButtonMap { int button; int bit; };
		static const ButtonMap map[] = {
			{ 0, DKEY_CROSS }, { 1, DKEY_CIRCLE },
			{ 2, DKEY_SQUARE }, { 3, DKEY_TRIANGLE },
			{ 4, DKEY_L1 }, { 5, DKEY_R1 },
			{ 6, DKEY_L2 }, { 7, DKEY_R2 },
			{ 8, DKEY_SELECT }, { 9, DKEY_START },
		};

		int buttons = SDL_JoystickNumButtons(joy);
		for (unsigned int i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
			if (map[i].button < buttons && SDL_JoystickGetButton(joy, map[i].button))
				state &= ~(1 << map[i].bit);
		}

		pad_state[port] = state;
	}
#endif
}

unsigned short pad_read(int num)
{
	return num == 0 ? pad_state[0] : pad_state[1];
}

void video_flip(void)
{
	if (!SCREEN)
		SCREEN = fallback_screen;
	if (emu_running && Config.ShowFps)
		port_printf(5, 5, pl_data.stats_msg);
	if (video_cb)
		video_cb(SCREEN, frame_width, frame_height, frame_pitch);
}

#ifdef GPU_DFXVIDEO
void video_set(unsigned short *pVideo, unsigned int width, unsigned int height)
{
	if (!SCREEN)
		SCREEN = fallback_screen;

	unsigned int copy_w = width > (unsigned int)frame_width ? frame_width : width;
	unsigned int copy_h = height > (unsigned int)frame_height ? frame_height : height;
	for (unsigned int y = 0; y < copy_h; ++y)
		memcpy(SCREEN + y * frame_pitch, pVideo + y * width, copy_w * sizeof(uint16_t));
	video_flip();
}
#endif

void video_clear(void)
{
	if (!SCREEN)
		SCREEN = fallback_screen;
	for (int y = 0; y < frame_height; ++y)
		memset(SCREEN + y * frame_pitch, 0, frame_width * sizeof(uint16_t));
}

void port_printf(int x, int y, const char *text)
{
	(void)x;
	(void)y;
	(void)text;
}

unsigned get_ticks(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

void wait_ticks(unsigned s)
{
	usleep(s * 1000u);
}

int SelectGame(void)
{
	return 0;
}

int GameMenu(void)
{
	return 0;
}
