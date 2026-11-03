#!/bin/sh
# thumbs.sh -- generate cover thumbnails from the first page of each PDF.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: thumbs.sh [-o COVER_DIR] [-w WIDTH] [-f] PDF...
#
#   -o COVER_DIR  where thumbnails go   (default: _site/covers)
#   -w WIDTH      pixel width           (default: 400)
#   -f            regenerate even when the thumbnail is newer than the PDF
#
# Requires poppler-utils (pdftoppm). Thumbnails are the expensive part of a
# PDF-browsing site, so they are skipped when already current; that keeps
# `make` fast once the archive is large.

set -eu

COVERS="_site/covers"
WIDTH=400
FORCE=false

while getopts 'o:w:fh' opt; do
    case "$opt" in
        o) COVERS="$OPTARG" ;;
        w) WIDTH="$OPTARG" ;;
        f) FORCE=true ;;
        h) sed -n '2,14p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { echo "usage: thumbs.sh [options] PDF..." >&2; exit 2; }

command -v pdftoppm >/dev/null 2>&1 || {
    echo "error: pdftoppm not found (apt install poppler-utils)" >&2; exit 1; }

mkdir -p "$COVERS"

for pdf in "$@"; do
    [ -f "$pdf" ] || { echo "warning: no such file: $pdf" >&2; continue; }
    base="$(basename "$pdf" .pdf)"
    out="$COVERS/$base.jpg"
    # `find -newer` rather than `[ -nt ]`, which POSIX sh does not define.
    if [ "$FORCE" = false ] && [ -f "$out" ] &&
       [ -z "$(find "$pdf" -newer "$out" 2>/dev/null)" ]; then
        continue
    fi
    pdftoppm -jpeg -r 150 -f 1 -l 1 -scale-to-x "$WIDTH" -scale-to-y -1 \
        -singlefile "$pdf" "${out%.jpg}"
    printf 'cover  %s\n' "$out" >&2
done
