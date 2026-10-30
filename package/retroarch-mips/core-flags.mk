# Included AFTER the upstream makefile. Environment CFLAGS alone are not
# sufficient: upstream may replace them or append -O2 after Buildroot's -O3.
# Retain required upstream feature defines/includes and PIC/shared linkage.
LILSMOKE_UPSTREAM_STRIP = -O%
ifeq ($(LILSMOKE_CORE_MIPS),1)
LILSMOKE_UPSTREAM_STRIP += -march=% -mtune=% -mips%
endif
override CFLAGS := $(filter-out $(LILSMOKE_UPSTREAM_STRIP),$(CFLAGS)) $(LILSMOKE_CORE_CFLAGS)
override CXXFLAGS := $(filter-out $(LILSMOKE_UPSTREAM_STRIP),$(CXXFLAGS)) $(LILSMOKE_CORE_CXXFLAGS)
override LDFLAGS += $(LILSMOKE_CORE_LDFLAGS)
ifeq ($(LILSMOKE_CORE_MIPS),1)
# A dlopen'ed core cannot borrow compiler runtime symbols from RetroArch.
# Let the cross driver link its matching libgcc even for upstream -nostdlib ports.
override SHARED := $(filter-out -nostdlib,$(SHARED))
# MIPS32r2 traps unaligned scalar loads. Use each core's alignment-safe path.
override CFLAGS := $(filter-out -DFAST_LSB_WORD_ACCESS -ffast-math -msse% -mavx%,$(CFLAGS)) -UFAST_LSB_WORD_ACCESS -fno-strict-aliasing
override CXXFLAGS := $(filter-out -DFAST_LSB_WORD_ACCESS -ffast-math -msse% -mavx%,$(CXXFLAGS)) -UFAST_LSB_WORD_ACCESS -fno-strict-aliasing
override CFLAGS += -DALIGN_LONG
override CXXFLAGS += -DALIGN_LONG
endif
