#!/bin/sh
# bootstrap.sh : compile verbs, populate CAS, generate world.data
set -e

CROSS="${CROSS:-m68k-linux-gnu-gcc}"
CFLAGS="-mcpu=5475 -O2 -nostdlib -static -T vm.ld"
STORE=depot

cp world.data.sample world.data

# Build cas_write helper if needed
if [ ! -x cas_write ]; then
    cc -o cas_write cas_write.c -Wall -Og smolvfs/cas.o smolvfs/cas-pack.o smolvfs/cas-tree.o smolvfs/vfs.o smolvfs/vfs-snap.o -lm
fi

for src in verb_*.c; do
    [ -f "$src" ] || continue
    base="${src%.c}"
    elf="${base}.elf"

    $CROSS $CFLAGS -o "$elf" "$src"

    hash=$(./cas_write "$elf" "$STORE")

    sed -i "s|{{${src}}}|b2:${hash}|g" world.data

    printf '%s -> %s\n' "$src" "$hash"
done

echo "bootstrap complete"
