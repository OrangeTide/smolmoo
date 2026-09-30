#!/bin/sh
# update-sdk.sh : smolmoo's re-vendoring wrapper around skjegg's vendor.sh.
# SPDX-License-Identifier: 0BSD OR CC0-1.0
#
# This is skjegg's vendor.sh with smolmoo's component selection and release
# ref baked in.  Run it with no arguments to re-vendor the SDK into sdk/;
# pass -r REF to move to a different release.  Regenerate it from a newer
# vendor.sh when the release changes the vendoring logic (component set,
# backend file layout), which a plain -r bump cannot follow.
#
# Usage: update-sdk.sh [-d DIR] [-r REF] [-u DIR] component ...
#
# Backends:  coldfire  riscv  mips
# Frontends: tinc  scheme  moo  pascal  cc
# Tools:     as  ld  as-rv  ld-rv  as-mips  ld-mips  ar  cpp
# Emulators: emu-cf  emu-rv
#
# smolmoo post-vendor steps (this script overwrites sdk/ wholesale, so these
# local integrations must be re-applied after each run):
#   1. Restore the smolmoo-local verb runtime that skjegg does not ship:
#        git checkout -- sdk/runtime/host_vm.c sdk/runtime/hypercall.S \
#          sdk/runtime/start_vm.S sdk/runtime/mulibc_vm.h sdk/runtime/moo_rt.c \
#          sdk/runtime/moo_syscall_rv.S sdk/runtime/verb_rt_rv.S \
#          sdk/runtime/verbmain.c
#   2. In sdk/skjegg.mk: wrap the cross-compiled runtime object rules in
#        ifndef SKJ_SKIP_M68K_RUNTIME ... endif  (smolmoo builds its own
#        runtime with skj-cc/skj-as and stays free of the m68k/riscv GNU
#        tools), and add the SPDX tag to the mk header.
#   3. Restore the smolmoo sdk/LICENSE (a copy of the repo LICENSE.md, the
#        dual-license summary) over upstream's full CC0 text:
#          git checkout HEAD -- sdk/LICENSE
#        A human may then normalize the vendored source headers to add the
#        smolmoo copyright line above each SPDX tag; that is left to a human.
#   4. Re-apply smolmoo's local MooScript patch to sdk/moo/lower.c: the
#        verb-call lowering (N_VCALL) emits __moo_verb_call(obj, verb, argc,
#        typemask, args...) with a per-argument typemask (2 bits/arg: 2 =
#        object, 1 = string, 0 = other), which host_vm.c uses to route object
#        args to dobj/iobj and a string arg to argstr.  Upstream skjegg has no
#        typemask.  The other toolchain patches smolmoo once carried (assembler
#        immediate-range checks, the backend's large frame-offset lowering)
#        are upstream as of v0.7.0 and no longer need re-applying.
#   5. Copy the server-embedded CPU core to the top level:
#        cp sdk/emu/rv32.c sdk/emu/rv32.h .
#   RISC-V RV32 is the verb engine.  As of skjegg v0.7.0 the RV stack calling
#   convention was retired: the server builds every verb with the psABI
#   toolchain (skj-cc-rv, skj-as-rv, skj-ld-rv).  The vendored ColdFire
#   components are unused by the server; trimming them from COMPONENTS is a
#   separate cleanup.

set -eu

ORIGIN="https://github.com/OrangeTide/skjegg-compiler-suite.git"
COMPONENTS="coldfire riscv moo cc as ld as-rv ld-rv cpp emu-cf emu-rv"
DEST="sdk"
REF="v0.7.0"
UPDATE_DIR="."

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
Usage: vendor-skjegg.sh [-d DIR] [-r REF] [-u DIR] component ...
       update-skjegg.sh                      (re-vendor)

Backends:  coldfire  riscv  mips
Frontends: tinc  scheme  moo  pascal  cc
Tools:     as  ld  as-rv  ld-rv  as-mips  ld-mips  ar  cpp
             (as-rv/ld-rv: the RISC-V RV32 toolchain; as-mips/ld-mips: the
              MIPS I toolchain, ld-mips writes ELF and PlayStation PS-EXE;
              ar: a target-neutral archive tool for any of the linkers)
Emulators: emu-cf  emu-rv    (skj-run; either, both, or neither)

Options:
  -d DIR   destination directory (default: skjegg)
  -r REF   git ref — branch or tag (default: main)
  -u DIR   install update-skjegg.sh to DIR (default: cwd)
  -h       show this help
USAGE
    exit 1
}

# ---- option parsing ----

_u_explicit=0
while getopts d:r:u:h opt; do
    case "$opt" in
        d) DEST="$OPTARG" ;;
        r) REF="$OPTARG" ;;
        u) UPDATE_DIR="$OPTARG"; _u_explicit=1 ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))

if [ $# -gt 0 ]; then
    COMPONENTS="$*"
fi

if [ -z "$COMPONENTS" ]; then
    die "no components specified (see -h)"
fi

# ---- parse and validate components ----

has_cf=0 has_rv=0 has_mips=0
has_tinc=0 has_scheme=0 has_moo=0 has_pascal=0 has_cc=0
has_as=0 has_ld=0 has_cpp=0
has_as_rv=0 has_ld_rv=0
has_as_mips=0 has_ld_mips=0 has_ar=0
has_emu_cf=0 has_emu_rv=0

for comp in $COMPONENTS; do
    case "$comp" in
        coldfire) has_cf=1 ;;
        riscv)    has_rv=1 ;;
        mips)     has_mips=1 ;;
        tinc)     has_tinc=1 ;;
        scheme)   has_scheme=1 ;;
        moo)      has_moo=1 ;;
        pascal)   has_pascal=1 ;;
        cc)       has_cc=1 ;;
        as)       has_as=1 ;;
        ld)       has_ld=1 ;;
        as-rv)    has_as_rv=1 ;;
        ld-rv)    has_ld_rv=1 ;;
        as-mips)  has_as_mips=1 ;;
        ld-mips)  has_ld_mips=1 ;;
        ar)       has_ar=1 ;;
        cpp)      has_cpp=1 ;;
        emu-cf)   has_emu_cf=1 ;;
        emu-rv)   has_emu_rv=1 ;;
        *)        die "unknown component: $comp" ;;
    esac
done

has_fe=$((has_tinc + has_scheme + has_moo + has_pascal + has_cc))
has_be=$((has_cf + has_rv + has_mips))

if [ "$has_fe" -gt 0 ] && [ "$has_be" -eq 0 ]; then
    die "frontends require at least one backend"
fi

need_cpp_lib=0
if [ "$has_cc" -eq 1 ] || [ "$has_cpp" -eq 1 ]; then
    need_cpp_lib=1
fi

# ---- fetch ----

command -v git >/dev/null || die "git is required"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

printf 'Fetching %s (ref: %s) ...\n' "$ORIGIN" "$REF"
git clone --quiet --depth 1 --branch "$REF" "$ORIGIN" "$tmpdir/src"
commit=$(git -C "$tmpdir/src" rev-parse --short HEAD)
printf '  commit %s\n' "$commit"

S="$tmpdir/src"

# ---- copy files ----

rm -rf "$DEST"

# version (always present; the tools' --version reads it, and it must come
# from skjegg rather than from the consuming project's git tags)
mkdir -p "$DEST"
cp "$S/version.mk" "$DEST/"

# IR core (always present)
mkdir -p "$DEST/ir"
cp "$S/ir/ir.c" "$S/ir/ir.h" "$S/ir/util.c" "$S/ir/util.h" \
   "$S/ir/arena.c" "$S/ir/arena.h" "$DEST/ir/"

# backends
if [ "$has_cf" -eq 1 ]; then
    mkdir -p "$DEST/backend" "$DEST/runtime"
    cp "$S/backend/cf_emit.c" "$S/backend/regalloc_cf.c" "$DEST/backend/"
    cp "$S/runtime/start.S" "$DEST/runtime/"
fi
if [ "$has_rv" -eq 1 ]; then
    mkdir -p "$DEST/backend" "$DEST/runtime"
    cp "$S/backend/rv_select.c" "$S/backend/rv_mc_text.c" \
       "$S/backend/rv_mc.h" "$S/backend/rv_select.h" "$S/backend/rv_mc_text.h" \
       "$S/backend/regalloc_rv.c" "$DEST/backend/"
    cp "$S/runtime/start_rv.S" "$DEST/runtime/"
fi
if [ "$has_mips" -eq 1 ]; then
    # The Linux crt (start_mips.S), plus what MIPS I specifically needs: half.c
    # (no hardware _Float16, so IR_FLH/FSH lower to these helpers) and
    # softfloat.c (the FPU-less soft-float path, for skj-cc-mips-sf and the
    # PlayStation R3051).  start_psx.S is the BIOS-TTY crt for the PS-EXE that
    # skj-ld-mips writes.
    mkdir -p "$DEST/backend" "$DEST/runtime"
    cp "$S/backend/mips_emit.c" "$S/backend/regalloc_mips.c" "$DEST/backend/"
    cp "$S/runtime/start_mips.S" "$S/runtime/start_psx.S" \
       "$S/runtime/half.c" "$S/runtime/softfloat.c" "$DEST/runtime/"
fi

# frontends
if [ "$has_tinc" -eq 1 ]; then
    mkdir -p "$DEST/tinc"
    cp "$S"/tinc/*.c "$S"/tinc/*.h "$DEST/tinc/"
fi
if [ "$has_scheme" -eq 1 ]; then
    mkdir -p "$DEST/scheme"
    cp "$S/scheme/lex.c" "$S/scheme/parse.c" "$S/scheme/print.c" \
       "$S/scheme/lower.c" "$S/scheme/main.c" "$S/scheme/gc.c" \
       "$S/scheme/gc.h" "$S/scheme/scheme.h" "$DEST/scheme/"
fi
if [ "$has_moo" -eq 1 ]; then
    mkdir -p "$DEST/moo" "$DEST/runtime"
    cp "$S"/moo/*.c "$S"/moo/*.h "$DEST/moo/"
    for f in "$S"/moo/*.md "$S"/moo/*.ebnf; do
        [ -e "$f" ] && cp "$f" "$DEST/moo/"
    done
    cp "$S/runtime/str.c" "$S/runtime/list.c" \
       "$S/runtime/host_stub.c" "$S/runtime/toy_host.c" "$DEST/runtime/"
fi
if [ "$has_pascal" -eq 1 ]; then
    mkdir -p "$DEST/pascal" "$DEST/runtime"
    cp "$S"/pascal/*.c "$S"/pascal/*.h "$DEST/pascal/"
    cp "$S/runtime/pascal_rt.c" "$DEST/runtime/"
fi
if [ "$has_cc" -eq 1 ]; then
    mkdir -p "$DEST/cc"
    cp "$S"/cc/*.c "$S"/cc/*.h "$DEST/cc/"
fi

# cpp library (needed by cc and cpp tool)
if [ "$need_cpp_lib" -eq 1 ]; then
    mkdir -p "$DEST/cpp"
    cp "$S/cpp/tok.c" "$S/cpp/macro.c" "$S/cpp/cond.c" "$S/cpp/dir.c" \
       "$S/cpp/cpp.h" "$S/cpp/internal.h" "$DEST/cpp/"
fi
if [ "$has_cpp" -eq 1 ]; then
    cp "$S/cpp/main.c" "$DEST/cpp/"
fi

# assembler.  The arch-neutral skeleton (asm_lex + asm_obj) is shared by the
# ColdFire assembler (as), the RISC-V one (as-rv) and the MIPS one (as-mips);
# each front end plus its standalone tool is taken separately, like the two
# emulator cores.
if [ "$has_as" -eq 1 ] || [ "$has_as_rv" -eq 1 ] || [ "$has_as_mips" -eq 1 ]; then
    mkdir -p "$DEST/as"
    cp "$S/as/asm_lex.c" "$S/as/asm_obj.c" \
       "$S/as/asm_lex.h" "$S/as/asm_obj.h" "$DEST/as/"
fi
if [ "$has_as" -eq 1 ]; then                    # ColdFire front end + tool
    cp "$S/as/parse.c" "$S/as/encode.c" "$S/as/elf.c" \
       "$S/as/as.h" "$S/as/main.c" "$DEST/as/"
fi
if [ "$has_as_rv" -eq 1 ]; then                 # RISC-V front end + tool
    cp "$S/as/rv_parse.c" "$S/as/rv_encode.c" "$S/as/rv_elf.c" \
       "$S/as/rv_macro.c" "$S/as/rv.h" "$S/as/rv_main.c" "$DEST/as/"
fi
if [ "$has_as_mips" -eq 1 ]; then               # MIPS front end + tool
    cp "$S/as/mips_parse.c" "$S/as/mips_encode.c" "$S/as/mips_elf.c" \
       "$S/as/mips_sched.c" "$S/as/mips.h" "$S/as/mips_main.c" "$DEST/as/"
fi

# linker.  The layout core, script parser and mapfile reader (link/script/
# mapfile) are shared by the ColdFire linker (ld), the RISC-V one (ld-rv) and
# the MIPS one (ld-mips); each target's ELF I/O plus its tool is taken
# separately.
if [ "$has_ld" -eq 1 ] || [ "$has_ld_rv" -eq 1 ] || [ "$has_ld_mips" -eq 1 ]; then
    mkdir -p "$DEST/ld"
    cp "$S/ld/script.c" "$S/ld/link.c" "$S/ld/mapfile.c" \
       "$S/ld/ld.h" "$S/ld/mapfile.h" "$DEST/ld/"
fi
if [ "$has_ld" -eq 1 ]; then                    # ColdFire ELF I/O + tool
    cp "$S/ld/elf_read.c" "$S/ld/elf_write.c" "$S/ld/main.c" "$DEST/ld/"
fi
if [ "$has_ld_rv" -eq 1 ]; then                 # RISC-V ELF I/O + reloc + tool
    cp "$S/ld/rv_elf_read.c" "$S/ld/rv_link.c" "$S/ld/rv_elf_write.c" \
       "$S/ld/rv_archive.c" "$S/ld/rv_ld.h" "$S/ld/rv_main.c" "$DEST/ld/"
fi
if [ "$has_ld_mips" -eq 1 ]; then               # MIPS ELF+PS-EXE I/O + reloc + tool
    cp "$S/ld/mips_elf_read.c" "$S/ld/mips_link.c" "$S/ld/mips_elf_write.c" \
       "$S/ld/mips_psexe_write.c" "$S/ld/mips_archive.c" "$S/ld/mips_ld.h" \
       "$S/ld/mips_main.c" "$DEST/ld/"
fi

# skj-ar, a target-neutral archive tool.  It needs only the mapfile reader from
# ld/, so a bare "ar" selection stays minimal; the copy is idempotent when a
# linker already brought mapfile in.
if [ "$has_ar" -eq 1 ]; then
    mkdir -p "$DEST/ld" "$DEST/ar"
    cp "$S/ld/mapfile.c" "$S/ld/mapfile.h" "$DEST/ld/"
    cp "$S/ar/main.c" "$DEST/ar/"
fi

# emulator (skj-run).  The two CPU cores are independent: a project that
# runs only ColdFire guests takes emu-cf and never carries the RV32 core.
if [ "$has_emu_cf" -eq 1 ] || [ "$has_emu_rv" -eq 1 ]; then
    mkdir -p "$DEST/emu"
    cp "$S/emu/main.c" "$S/emu/guest.c" "$S/emu/guest.h" \
       "$S/emu/elf32.c" "$S/emu/elf32.h" "$DEST/emu/"
fi
if [ "$has_emu_cf" -eq 1 ]; then
    cp "$S/emu/coldfire.c" "$S/emu/coldfire.h" "$S/emu/cf_user.c" "$DEST/emu/"
fi
if [ "$has_emu_rv" -eq 1 ]; then
    cp "$S/emu/rv32.c" "$S/emu/rv32.h" "$S/emu/rv_user.c" "$DEST/emu/"
fi

# LICENSE
if [ -f "$S/LICENSE" ]; then
    cp "$S/LICENSE" "$DEST/"
fi

# ---- generate update-skjegg.sh ----

if [ -n "$UPDATE_DIR" ]; then
    _upd_dir="$UPDATE_DIR"
else
    _upd_dir="$(dirname "$0")"
fi

if [ "$_u_explicit" -eq 1 ]; then
    _baked_upd="$UPDATE_DIR"
else
    _baked_upd=""
fi

tmp_upd=$(mktemp)
sed -e "s|^ORIGIN=.*|ORIGIN=\"$ORIGIN\"|" \
    -e "s|^COMPONENTS=.*|COMPONENTS=\"$COMPONENTS\"|" \
    -e "s|^DEST=.*|DEST=\"$DEST\"|" \
    -e "s|^REF=.*|REF=\"$REF\"|" \
    -e "s|^UPDATE_DIR=.*|UPDATE_DIR=\"$_baked_upd\"|" \
    "$0" > "$tmp_upd"
chmod +x "$tmp_upd"
mkdir -p "$_upd_dir"
mv "$tmp_upd" "$_upd_dir/update-skjegg.sh"

# ---- generate skjegg.mk ----

MK="$DEST/skjegg.mk"

emit_compiler() {
    _name="$1"; _fe="$2"; _be="$3"; _inc="$4"; _extra="${5:-}"
    if [ -n "$_extra" ]; then
        _all_src="\$(SKJ_IR) \$($_be) \$($_fe) $_extra"
    else
        _all_src="\$(SKJ_IR) \$($_be) \$($_fe)"
    fi
    {
        printf '\nSKJ_ALL += $(BUILD)/%s\n' "$_name"
        printf '$(BUILD)/%s: %s | $(BUILD)\n' "$_name" "$_all_src"
        printf '\t$(CC) $(CFLAGS) %s -o $@ %s\n' "$_inc" "$_all_src"
    } >> "$MK"
}

# header
cat > "$MK" <<EOF
# skjegg.mk — vendored skjegg compiler toolkit
# Origin: $ORIGIN ($commit)
# Components: $COMPONENTS
# Generated: $(date -u +%Y-%m-%d)
#
# Include from your Makefile:
#   include $DEST/skjegg.mk
#
# Then build with:
#   make skjegg

SKJEGG := \$(dir \$(lastword \$(MAKEFILE_LIST)))
BUILD  ?= build

CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS += -I\$(BUILD)

# The vendored skjegg version.  Fixed at vendoring time; deliberately not
# derived from git, since git here is your project, not skjegg.
#
# SKJ_VERSION names the release.  The commit is baked in beside it because
# a vendored tree may come from a ref that has moved past that release, and
# a bare release number would then overstate what you actually have.
include \$(SKJEGG)version.mk
SKJ_VENDOR_REF    := $REF
SKJ_VENDOR_COMMIT := $commit
SKJ_FULL_VERSION  := \$(SKJ_VERSION)\$(if \$(SKJ_VENDOR_COMMIT),+g\$(SKJ_VENDOR_COMMIT))
EOF

# cross-toolchain variables
if [ "$has_cf" -eq 1 ]; then
    cat >> "$MK" <<'MK'

M68K_CC ?= m68k-linux-gnu-gcc
M68K_AS ?= m68k-linux-gnu-as
M68K_LD ?= m68k-linux-gnu-ld
MK
fi
if [ "$has_rv" -eq 1 ]; then
    cat >> "$MK" <<'MK'

RV_AS ?= riscv64-linux-gnu-as
RV_LD ?= riscv64-linux-gnu-ld
MK
fi
if [ "$has_mips" -eq 1 ]; then
    cat >> "$MK" <<'MK'

MIPS_CC ?= mipsel-none-elf-gcc
MIPS_AS ?= mipsel-none-elf-as
MIPS_LD ?= mipsel-none-elf-ld
MK
fi

# source variables
{
    printf '\n# sources\n'
    printf 'SKJ_IR := $(SKJEGG)ir/ir.c $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
} >> "$MK"

if [ "$has_cf" -eq 1 ]; then
    printf 'SKJ_CF := $(SKJEGG)backend/regalloc_cf.c $(SKJEGG)backend/cf_emit.c\n' >> "$MK"
fi
if [ "$has_rv" -eq 1 ]; then
    printf 'SKJ_RV := $(SKJEGG)backend/regalloc_rv.c $(SKJEGG)backend/rv_select.c $(SKJEGG)backend/rv_mc_text.c\n' >> "$MK"
fi
if [ "$has_mips" -eq 1 ]; then
    printf 'SKJ_MIPS := $(SKJEGG)backend/regalloc_mips.c $(SKJEGG)backend/mips_emit.c\n' >> "$MK"
fi

if [ "$has_tinc" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_TINC := $(SKJEGG)tinc/lex.c $(SKJEGG)tinc/parse.c \
            $(SKJEGG)tinc/lower.c $(SKJEGG)tinc/main.c
MK
fi
if [ "$has_scheme" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_SCHEME := $(SKJEGG)scheme/lex.c $(SKJEGG)scheme/parse.c \
              $(SKJEGG)scheme/print.c $(SKJEGG)scheme/lower.c \
              $(SKJEGG)scheme/main.c $(SKJEGG)scheme/gc.c
MK
fi
if [ "$has_moo" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_MOO := $(SKJEGG)moo/lex.c $(SKJEGG)moo/parse.c $(SKJEGG)moo/typecheck.c \
           $(SKJEGG)moo/lower.c $(SKJEGG)moo/main.c
MK
fi
if [ "$has_pascal" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_PASCAL := $(SKJEGG)pascal/lex.c $(SKJEGG)pascal/parse.c \
              $(SKJEGG)pascal/lower.c $(SKJEGG)pascal/main.c
MK
fi
if [ "$has_cc" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_CC := $(SKJEGG)cc/lex.c $(SKJEGG)cc/parse.c $(SKJEGG)cc/type.c \
          $(SKJEGG)cc/lower.c $(SKJEGG)cc/main.c
MK
fi
if [ "$need_cpp_lib" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_CPP_LIB := $(SKJEGG)cpp/tok.c $(SKJEGG)cpp/macro.c \
               $(SKJEGG)cpp/cond.c $(SKJEGG)cpp/dir.c
MK
fi
if [ "$has_as" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_AS_LIB := $(SKJEGG)as/asm_lex.c $(SKJEGG)as/asm_obj.c \
              $(SKJEGG)as/parse.c $(SKJEGG)as/encode.c $(SKJEGG)as/elf.c
MK
fi
if [ "$has_ld" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_LD_LIB := $(SKJEGG)ld/elf_read.c $(SKJEGG)ld/script.c \
              $(SKJEGG)ld/link.c $(SKJEGG)ld/elf_write.c \
              $(SKJEGG)ld/mapfile.c
MK
fi
if [ "$has_as_rv" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_AS_RV_LIB := $(SKJEGG)as/asm_lex.c $(SKJEGG)as/asm_obj.c \
                 $(SKJEGG)as/rv_parse.c $(SKJEGG)as/rv_encode.c \
                 $(SKJEGG)as/rv_macro.c $(SKJEGG)as/rv_elf.c
MK
fi
if [ "$has_ld_rv" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_LD_RV_LIB := $(SKJEGG)ld/rv_elf_read.c $(SKJEGG)ld/rv_link.c \
                 $(SKJEGG)ld/rv_elf_write.c $(SKJEGG)ld/rv_archive.c \
                 $(SKJEGG)ld/link.c $(SKJEGG)ld/script.c $(SKJEGG)ld/mapfile.c
MK
fi
if [ "$has_as_mips" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_AS_MIPS_LIB := $(SKJEGG)as/asm_lex.c $(SKJEGG)as/asm_obj.c \
                   $(SKJEGG)as/mips_parse.c $(SKJEGG)as/mips_encode.c \
                   $(SKJEGG)as/mips_elf.c $(SKJEGG)as/mips_sched.c
MK
fi
if [ "$has_ld_mips" -eq 1 ]; then
    cat >> "$MK" <<'MK'
SKJ_LD_MIPS_LIB := $(SKJEGG)ld/mips_elf_read.c $(SKJEGG)ld/mips_link.c \
                   $(SKJEGG)ld/mips_elf_write.c $(SKJEGG)ld/mips_psexe_write.c \
                   $(SKJEGG)ld/mips_archive.c $(SKJEGG)ld/link.c \
                   $(SKJEGG)ld/script.c $(SKJEGG)ld/mapfile.c
MK
fi

# compiler binary rules (frontend x backend)
printf '\nSKJ_ALL :=\n' >> "$MK"

if [ "$has_tinc" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    emit_compiler skj-tinc SKJ_TINC SKJ_CF '-I$(SKJEGG)ir -I$(SKJEGG)tinc'
fi
if [ "$has_tinc" -eq 1 ] && [ "$has_rv" -eq 1 ]; then
    emit_compiler skj-tinc-rv SKJ_TINC SKJ_RV '-I$(SKJEGG)ir -I$(SKJEGG)tinc'
fi
if [ "$has_tinc" -eq 1 ] && [ "$has_mips" -eq 1 ]; then
    emit_compiler skj-tinc-mips SKJ_TINC SKJ_MIPS '-I$(SKJEGG)ir -I$(SKJEGG)tinc'
fi
if [ "$has_scheme" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    emit_compiler skj-sc SKJ_SCHEME SKJ_CF '-I$(SKJEGG)scheme -I$(SKJEGG)ir'
fi
if [ "$has_scheme" -eq 1 ] && [ "$has_rv" -eq 1 ]; then
    emit_compiler skj-sc-rv SKJ_SCHEME SKJ_RV '-I$(SKJEGG)scheme -I$(SKJEGG)ir'
fi
if [ "$has_scheme" -eq 1 ] && [ "$has_mips" -eq 1 ]; then
    emit_compiler skj-sc-mips SKJ_SCHEME SKJ_MIPS '-I$(SKJEGG)scheme -I$(SKJEGG)ir'
fi
if [ "$has_moo" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    emit_compiler skj-mooc SKJ_MOO SKJ_CF '-I$(SKJEGG)moo -I$(SKJEGG)ir'
fi
if [ "$has_moo" -eq 1 ] && [ "$has_rv" -eq 1 ]; then
    emit_compiler skj-mooc-rv SKJ_MOO SKJ_RV '-I$(SKJEGG)moo -I$(SKJEGG)ir'
fi
if [ "$has_moo" -eq 1 ] && [ "$has_mips" -eq 1 ]; then
    emit_compiler skj-mooc-mips SKJ_MOO SKJ_MIPS '-I$(SKJEGG)moo -I$(SKJEGG)ir'
fi
if [ "$has_pascal" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    emit_compiler skj-pc SKJ_PASCAL SKJ_CF '-I$(SKJEGG)pascal -I$(SKJEGG)ir'
fi
if [ "$has_pascal" -eq 1 ] && [ "$has_rv" -eq 1 ]; then
    emit_compiler skj-pc-rv SKJ_PASCAL SKJ_RV '-I$(SKJEGG)pascal -I$(SKJEGG)ir'
fi
if [ "$has_pascal" -eq 1 ] && [ "$has_mips" -eq 1 ]; then
    emit_compiler skj-pc-mips SKJ_PASCAL SKJ_MIPS '-I$(SKJEGG)pascal -I$(SKJEGG)ir'
fi
if [ "$has_cc" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    emit_compiler skj-cc SKJ_CC SKJ_CF \
        '-I$(SKJEGG)cc -I$(SKJEGG)cpp -I$(SKJEGG)ir' '$(SKJ_CPP_LIB)'
fi
if [ "$has_cc" -eq 1 ] && [ "$has_rv" -eq 1 ]; then
    # The RISC-V C compiler emits the standard ILP32 psABI (a0..a7) so its
    # output interlinks with gcc; it pairs with the standalone RV toolchain
    # (as-rv/ld-rv) and runtime/start_rv_psabi_cc.S.
    emit_compiler skj-cc-rv SKJ_CC SKJ_RV \
        '-I$(SKJEGG)cc -I$(SKJEGG)cpp -I$(SKJEGG)ir' '$(SKJ_CPP_LIB)'
fi
if [ "$has_cc" -eq 1 ] && [ "$has_mips" -eq 1 ]; then
    emit_compiler skj-cc-mips SKJ_CC SKJ_MIPS \
        '-I$(SKJEGG)cc -I$(SKJEGG)cpp -I$(SKJEGG)ir' '$(SKJ_CPP_LIB)'
    # The soft-float build: same sources with -DMIPS_SOFTFLOAT, emitting no
    # coprocessor-1 instruction and lowering every float op to a runtime/
    # softfloat.c call, for the FPU-less R3051 (the PlayStation).
    emit_compiler skj-cc-mips-sf SKJ_CC SKJ_MIPS \
        '-DMIPS_SOFTFLOAT -I$(SKJEGG)cc -I$(SKJEGG)cpp -I$(SKJEGG)ir' '$(SKJ_CPP_LIB)'
fi

# tool binary rules
if [ "$has_cpp" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-cpp\n'
        printf '$(BUILD)/skj-cpp: $(SKJEGG)cpp/main.c $(SKJ_CPP_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)cpp -I$(SKJEGG)ir -o $@ $(SKJEGG)cpp/main.c $(SKJ_CPP_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_as" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-as\n'
        printf '$(BUILD)/skj-as: $(SKJEGG)as/main.c $(SKJ_AS_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)as -I$(SKJEGG)ir -o $@ $(SKJEGG)as/main.c $(SKJ_AS_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_ld" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-ld\n'
        printf '$(BUILD)/skj-ld: $(SKJEGG)ld/main.c $(SKJ_LD_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)ld -I$(SKJEGG)ir -o $@ $(SKJEGG)ld/main.c $(SKJ_LD_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_as_rv" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-as-rv\n'
        printf '$(BUILD)/skj-as-rv: $(SKJEGG)as/rv_main.c $(SKJ_AS_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)as -I$(SKJEGG)ir -o $@ $(SKJEGG)as/rv_main.c $(SKJ_AS_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_ld_rv" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-ld-rv\n'
        printf '$(BUILD)/skj-ld-rv: $(SKJEGG)ld/rv_main.c $(SKJ_LD_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)ld -I$(SKJEGG)ir -o $@ $(SKJEGG)ld/rv_main.c $(SKJ_LD_RV_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_as_mips" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-as-mips\n'
        printf '$(BUILD)/skj-as-mips: $(SKJEGG)as/mips_main.c $(SKJ_AS_MIPS_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)as -I$(SKJEGG)ir -o $@ $(SKJEGG)as/mips_main.c $(SKJ_AS_MIPS_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_ld_mips" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-ld-mips\n'
        printf '$(BUILD)/skj-ld-mips: $(SKJEGG)ld/mips_main.c $(SKJ_LD_MIPS_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)ld -I$(SKJEGG)ir -o $@ $(SKJEGG)ld/mips_main.c $(SKJ_LD_MIPS_LIB) $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi
if [ "$has_ar" -eq 1 ]; then
    {
        printf '\nSKJ_ALL += $(BUILD)/skj-ar\n'
        printf '$(BUILD)/skj-ar: $(SKJEGG)ar/main.c $(SKJEGG)ld/mapfile.c $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -I$(SKJEGG)ld -I$(SKJEGG)ir -o $@ $(SKJEGG)ar/main.c $(SKJEGG)ld/mapfile.c $(SKJEGG)ir/util.c $(SKJEGG)ir/arena.c\n'
    } >> "$MK"
fi

# emulator driver
if [ "$has_emu_cf" -eq 1 ] || [ "$has_emu_rv" -eq 1 ]; then
    _emu_src='$(SKJEGG)emu/main.c $(SKJEGG)emu/guest.c $(SKJEGG)emu/elf32.c'
    if [ "$has_emu_cf" -eq 1 ]; then
        _emu_src="$_emu_src \$(SKJEGG)emu/coldfire.c \$(SKJEGG)emu/cf_user.c"
    fi
    if [ "$has_emu_rv" -eq 1 ]; then
        _emu_src="$_emu_src \$(SKJEGG)emu/rv32.c \$(SKJEGG)emu/rv_user.c"
    fi
    {
        printf '\n# skj-run: the guest runner.  The architectures compiled in\n'
        printf '# are fixed at vendoring time; skj-run reads the ELF header to\n'
        printf '# pick between them.\n'
        printf 'SKJ_EMU := %s\n' "$_emu_src"
        printf 'SKJ_ALL += $(BUILD)/skj-run\n'
        printf '$(BUILD)/skj-run: $(SKJ_EMU) | $(BUILD)\n'
        printf '\t$(CC) $(CFLAGS) -DEMU_COLDFIRE=%s -DEMU_RV32=%s -I$(SKJEGG)emu -o $@ $(SKJ_EMU) -lm\n' \
            "$has_emu_cf" "$has_emu_rv"
    } >> "$MK"
fi

# cross-compiled runtime objects
if [ "$has_cf" -eq 1 ]; then
    {
        printf '\n$(BUILD)/start.o: $(SKJEGG)runtime/start.S | $(BUILD)\n'
        printf '\t$(M68K_AS) -o $@ $<\n'
    } >> "$MK"
fi
if [ "$has_rv" -eq 1 ]; then
    {
        printf '\n$(BUILD)/start_rv.o: $(SKJEGG)runtime/start_rv.S | $(BUILD)\n'
        printf '\t$(RV_AS) -march=rv32im -mabi=ilp32 -o $@ $<\n'
    } >> "$MK"
fi
if [ "$has_pascal" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    {
        printf '\n$(BUILD)/pascal_rt.o: $(SKJEGG)runtime/pascal_rt.c | $(BUILD)\n'
        printf '\t$(M68K_CC) -std=c99 -O2 -Wall -ffreestanding -c -o $@ $<\n'
    } >> "$MK"
fi
if [ "$has_moo" -eq 1 ] && [ "$has_cf" -eq 1 ]; then
    {
        printf '\n$(BUILD)/str.o: $(SKJEGG)runtime/str.c | $(BUILD)\n'
        printf '\t$(M68K_CC) -std=c99 -O2 -Wall -ffreestanding -c -o $@ $<\n'
        printf '\n$(BUILD)/list.o: $(SKJEGG)runtime/list.c | $(BUILD)\n'
        printf '\t$(M68K_CC) -std=c99 -O2 -Wall -ffreestanding -c -o $@ $<\n'
        printf '\n$(BUILD)/host_stub.o: $(SKJEGG)runtime/host_stub.c | $(BUILD)\n'
        printf '\t$(M68K_CC) -std=c99 -O2 -Wall -ffreestanding -c -o $@ $<\n'
        printf '\n$(BUILD)/toy_host.o: $(SKJEGG)runtime/toy_host.c | $(BUILD)\n'
        printf '\t$(M68K_CC) -std=c99 -O2 -Wall -ffreestanding -c -o $@ $<\n'
    } >> "$MK"
fi

# version header, phony and directory targets
cat >> "$MK" <<'MK'

# Every tool's main.c includes "version.h", so generate it from the release
# number plus the commit this tree was vendored from.
$(BUILD)/version.h: $(SKJEGG)version.mk $(SKJEGG)skjegg.mk | $(BUILD)
	@printf '#ifndef SKJ_VERSION_H\n#define SKJ_VERSION_H\n#define SKJ_VERSION "%s"\n#endif\n' '$(SKJ_FULL_VERSION)' > $@

$(SKJ_ALL): $(BUILD)/version.h

skjegg: $(SKJ_ALL)
.PHONY: skjegg

$(BUILD):
	mkdir -p $@
MK

# ---- summary ----

printf '\nVendored into %s/ (%s)\n' "$DEST" "$commit"
printf 'Components: %s\n' "$COMPONENTS"
printf 'Generated:  %s/skjegg.mk\n' "$DEST"
printf 'Update:     %s/update-skjegg.sh\n' "$_upd_dir"
