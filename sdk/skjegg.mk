# skjegg.mk — vendored skjegg compiler toolkit
# SPDX-License-Identifier: 0BSD OR CC0-1.0
# Origin: https://github.com/OrangeTide/skjegg-compiler-suite.git (cf186b4)
# Components: riscv moo cc as-rv ld-rv cpp emu-rv
# Generated: 2026-09-30
#
# Include from your Makefile:
#   include sdk/skjegg.mk
#
# Then build with:
#   make skjegg

SKJEGG := $(dir $(lastword $(MAKEFILE_LIST)))
BUILD  ?= build

CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS += -I$(BUILD)

# The vendored skjegg version.  Fixed at vendoring time; deliberately not
# derived from git, since git here is your project, not skjegg.
#
# SKJ_VERSION names the release.  The commit is baked in beside it because
# a vendored tree may come from a ref that has moved past that release, and
# a bare release number would then overstate what you actually have.
include $(SKJEGG)version.mk
SKJ_VENDOR_REF    := v0.7.0
SKJ_VENDOR_COMMIT := cf186b4
SKJ_FULL_VERSION  := $(SKJ_VERSION)$(if $(SKJ_VENDOR_COMMIT),+g$(SKJ_VENDOR_COMMIT))

RV_AS ?= riscv64-linux-gnu-as
RV_LD ?= riscv64-linux-gnu-ld

# sources
SKJ_IR := $(SKJEGG)ir/ir.c $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c
SKJ_RV := $(SKJEGG)backend/regalloc_rv.c $(SKJEGG)backend/rv_select.c $(SKJEGG)backend/rv_mc_text.c
SKJ_MOO := $(SKJEGG)moo/lex.c $(SKJEGG)moo/parse.c $(SKJEGG)moo/typecheck.c \
           $(SKJEGG)moo/lower.c $(SKJEGG)moo/main.c
SKJ_CC := $(SKJEGG)cc/lex.c $(SKJEGG)cc/parse.c $(SKJEGG)cc/type.c \
          $(SKJEGG)cc/lower.c $(SKJEGG)cc/main.c
SKJ_CPP_LIB := $(SKJEGG)cpp/tok.c $(SKJEGG)cpp/macro.c \
               $(SKJEGG)cpp/cond.c $(SKJEGG)cpp/dir.c
SKJ_AS_RV_LIB := $(SKJEGG)as/asm_lex.c $(SKJEGG)as/asm_obj.c \
                 $(SKJEGG)as/rv_parse.c $(SKJEGG)as/rv_encode.c \
                 $(SKJEGG)as/rv_macro.c $(SKJEGG)as/rv_elf.c
SKJ_LD_RV_LIB := $(SKJEGG)ld/rv_elf_read.c $(SKJEGG)ld/rv_link.c \
                 $(SKJEGG)ld/rv_elf_write.c $(SKJEGG)ld/rv_archive.c \
                 $(SKJEGG)ld/link.c $(SKJEGG)ld/script.c $(SKJEGG)ld/mapfile.c

SKJ_ALL :=

SKJ_ALL += $(BUILD)/skj-mooc-rv
$(BUILD)/skj-mooc-rv: $(SKJ_IR) $(SKJ_RV) $(SKJ_MOO) | $(BUILD)
	$(CC) $(CFLAGS) -I$(SKJEGG)moo -I$(SKJEGG)ir -o $@ $(SKJ_IR) $(SKJ_RV) $(SKJ_MOO)

SKJ_ALL += $(BUILD)/skj-cc-rv
$(BUILD)/skj-cc-rv: $(SKJ_IR) $(SKJ_RV) $(SKJ_CC) $(SKJ_CPP_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -I$(SKJEGG)cc -I$(SKJEGG)cpp -I$(SKJEGG)ir -o $@ $(SKJ_IR) $(SKJ_RV) $(SKJ_CC) $(SKJ_CPP_LIB)

SKJ_ALL += $(BUILD)/skj-cpp
$(BUILD)/skj-cpp: $(SKJEGG)cpp/main.c $(SKJ_CPP_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(SKJEGG)cpp -I$(SKJEGG)ir -o $@ $(SKJEGG)cpp/main.c $(SKJ_CPP_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c

SKJ_ALL += $(BUILD)/skj-as-rv
$(BUILD)/skj-as-rv: $(SKJEGG)as/rv_main.c $(SKJ_AS_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(SKJEGG)as -I$(SKJEGG)ir -o $@ $(SKJEGG)as/rv_main.c $(SKJ_AS_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c

SKJ_ALL += $(BUILD)/skj-ld-rv
$(BUILD)/skj-ld-rv: $(SKJEGG)ld/rv_main.c $(SKJ_LD_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(SKJEGG)ld -I$(SKJEGG)ir -o $@ $(SKJEGG)ld/rv_main.c $(SKJ_LD_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c

# skj-run: the guest runner.  The architectures compiled in
# are fixed at vendoring time; skj-run reads the ELF header to
# pick between them.
SKJ_EMU := $(SKJEGG)emu/main.c $(SKJEGG)emu/guest.c $(SKJEGG)emu/elf32.c $(SKJEGG)emu/rv32.c $(SKJEGG)emu/rv_user.c
SKJ_ALL += $(BUILD)/skj-run
$(BUILD)/skj-run: $(SKJ_EMU) | $(BUILD)
	$(CC) $(CFLAGS) -DEMU_COLDFIRE=0 -DEMU_RV32=1 -I$(SKJEGG)emu -o $@ $(SKJ_EMU) -lm

# Cross-assembled RV runtime object, built with the GNU RISC-V assembler.
# smolmoo builds its own verb runtime with the vendored skj-as-rv and does not
# use start_rv.o, so it sets SKJ_SKIP_M68K_RUNTIME to skip this rule and stay
# free of the GNU cross tools. (Local edit re-applied after each re-vendor;
# see update-sdk.sh.)
ifndef SKJ_SKIP_M68K_RUNTIME
$(BUILD)/start_rv.o: $(SKJEGG)runtime/start_rv.S | $(BUILD)
	$(RV_AS) -march=rv32im -mabi=ilp32 -o $@ $<
endif

# Every tool's main.c includes "version.h", so generate it from the release
# number plus the commit this tree was vendored from.
$(BUILD)/version.h: $(SKJEGG)version.mk $(SKJEGG)skjegg.mk | $(BUILD)
	@printf '#ifndef SKJ_VERSION_H\n#define SKJ_VERSION_H\n#define SKJ_VERSION "%s"\n#endif\n' '$(SKJ_FULL_VERSION)' > $@

$(SKJ_ALL): $(BUILD)/version.h

skjegg: $(SKJ_ALL)
.PHONY: skjegg

$(BUILD):
	mkdir -p $@
