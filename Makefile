# makefile for smolmoo
B := _build
BUILD := $(B)/sdk
OBJCOPY ?= objcopy
STRIP ?= strip
RM ?= rm -f
CFLAGS := -Wall -Wextra -g -Og -fno-omit-frame-pointer
SMOLVFS_OBJS := $(B)/smolvfs/cas.o $(B)/smolvfs/cas-pack.o $(B)/smolvfs/cas-omap.o \
	$(B)/smolvfs/cas-codec.o
OBJS := $(B)/smolmoo.o $(B)/rv32.o $(B)/monocypher.o $(SMOLVFS_OBJS)

all: $(B)/smolmoo
.DEFAULT_GOAL := all

SKJ_SKIP_M68K_RUNTIME := 1
include sdk/skjegg.mk
$(B)/smolmoo: $(OBJS) | $(B)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -lm
	$(OBJCOPY) --only-keep-debug $@ $@.debug
	$(STRIP) --strip-debug --strip-unneeded $@
	$(OBJCOPY) --add-gnu-debuglink=$@.debug $@
$(B)/smolmoo.o : smolmoo.c rv32.h monocypher.h smolvfs/cas-omap.h | $(B)
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
$(B)/monocypher.o : monocypher.c monocypher.h | $(B)
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
$(B)/rv32.o : rv32.c rv32.h | $(B)
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
$(B)/smolvfs/cas.o : smolvfs/cas.c smolvfs/cas.h smolvfs/cas-pack.h smolvfs/cas-codec.h | $(B)/smolvfs
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
$(B)/smolvfs/cas-pack.o : smolvfs/cas-pack.c smolvfs/cas-pack.h smolvfs/cas-codec.h | $(B)/smolvfs
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
$(B)/smolvfs/cas-omap.o : smolvfs/cas-omap.c smolvfs/cas-omap.h smolvfs/cas.h | $(B)/smolvfs
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
$(B)/smolvfs/cas-codec.o : smolvfs/cas-codec.c smolvfs/cas-codec.h smolvfs/cas.h | $(B)/smolvfs
	$(CC) -c -o $@ -MMD -MF $(@:.o=.dep) $(CFLAGS) $(CPPFLAGS) $<
# RV32 verb runtime: the ecall syscall stubs plus the 64-bit integer helpers,
# linked with every self-contained .c verb. Built with the in-tree RISC-V
# assembler.
sdk: skjegg $(BUILD)/verb_rt_rv.o

$(BUILD)/verb_rt_rv.o: sdk/runtime/verb_rt_rv.S | $(BUILD)
	$(BUILD)/skj-as-rv -o $@ $<

install: $(B)/smolmoo sdk
	$(B)/smolmoo install verbs.conf

$(B):
	mkdir -p $@
$(B)/smolvfs: | $(B)
	mkdir -p $@

BUNDLE := $(B)/smolmoo.tar.bz2
bundle: install
	tar cjf $(BUNDLE) -C $(B) smolmoo sdk \
	    -C $(CURDIR) depot index.html

clean clean-all:
	$(RM) -r $(B)
	$(RM) *.elf

# C unit tests (TAP). Vendored taptest driver runs test/test_*.c binaries.
# See test/README.md.
TAPTEST := $(B)/taptest
$(TAPTEST): test/taptest.c test/taptest_selftest.c test/taptest.h | $(B)
	$(CC) $(CFLAGS) -o $@ test/taptest.c test/taptest_selftest.c
$(B)/test_rv32: test/test_rv32.c test/testmain.c rv32.c rv32.h test/test.h | $(B)
	$(CC) $(CFLAGS) -I. -Itest -o $@ test/test_rv32.c test/testmain.c rv32.c -lm
$(B)/test_chromesix: test/test_chromesix.c test/testmain.c chromesix_verb.h mulibc.h test/test.h | $(B)
	$(CC) $(CFLAGS) -I. -Itest -o $@ test/test_chromesix.c test/testmain.c
CTESTS := $(B)/test_rv32 $(B)/test_chromesix
ctest: $(TAPTEST) $(CTESTS)
	$(TAPTEST) --self-test --exe $(CTESTS)

test: install ctest
	./test.sh
smoke: install
	./test.sh
run: $(B)/smolmoo
	$(B)/smolmoo serve
.PHONY: clean clean-all test smoke run sdk install bundle ctest
-include $(OBJS:.o=.dep)
