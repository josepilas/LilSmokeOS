# Included after upstream Makefile, for controlled compiler comparisons only.
override CFLAGS := $(filter-out -O% -ffast-math -DFAST_LSB_WORD_ACCESS,$(CFLAGS)) $(PERF_OPT) $(PERF_LTO) -DLSB_FIRST -fno-strict-aliasing -mips32r2 -mtune=74kc -mfp32 -mhard-float
override LDFLAGS += $(PERF_LTO)
