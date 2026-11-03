#!/bin/sh
# build-diagrams.sh -- render diagram and plot sources to SVG.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: build-diagrams.sh [-k] [-o OUT.svg] SOURCE...
#
#   -k         keep going when a renderer is missing (warn, skip that file)
#   -o OUT     output path; only valid with a single SOURCE
#   SOURCE...  one or more diagram sources, dispatched by extension
#
# By default the SVG is written next to its source, which is what makes the
# rest of the pipeline work without configuration: build-site.sh copies
# non-Markdown companion files into the output, so `![](graph.svg)` resolves
# in the editor, on the GitHub blob page, and in the built site.
#
#   .dot .gv       graphviz     apt install graphviz
#   .msc           mscgen       apt install mscgen
#   .plt .gnuplot  gnuplot      apt install gnuplot
#   .pic           pikchr, or groff -p as a fallback (groff is near-universal)
#   .d2            d2           single Go binary, no runtime      (unverified)
#   .mmd .mermaid  mmdc         npm install -g @mermaid-js/mermaid-cli (unverified)
#
# "unverified" means the dispatch was written from the tool's documentation but
# never executed, because neither tool was installable on the machine where
# this was built. graphviz, gnuplot, mscgen, and pikchr are exercised. If a d2
# or mermaid figure misbehaves, suspect the invocation here first.
#
# A missing renderer is an error, not a warning: a silently absent figure looks
# like a working page until someone reads it. Use -k when a contributor should
# be able to build the prose without installing every tool, but never in CI.
#
# Graphviz layout engine: the default is `dot`. Override per file with a magic
# first line, which keeps the choice with the diagram instead of in the build:
#
#   // pages-site: engine=neato

set -eu

KEEP_GOING=false
OUT=""

while getopts 'ko:h' opt; do
    case "$opt" in
        k) KEEP_GOING=true ;;
        o) OUT="$OPTARG" ;;
        h) sed -n '2,32p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { echo "usage: build-diagrams.sh [-k] [-o OUT.svg] SOURCE..." >&2; exit 2; }
[ -n "$OUT" ] && [ $# -gt 1 ] && { echo "error: -o takes a single source" >&2; exit 2; }

# A renderer that dies partway leaves its temp file behind, and those
# accumulate in the content tree where they are easy to commit by accident.
CURRENT_TMP=""
trap 'rm -f "$CURRENT_TMP"' EXIT INT TERM

# need TOOL PACKAGE -- true when the renderer is present; otherwise either
# fails or, under -k, reports and tells the caller to skip.
need() {
    command -v "$1" >/dev/null 2>&1 && return 0
    if [ "$KEEP_GOING" = true ]; then
        printf 'warning: %s not installed (%s); skipping\n' "$1" "$2" >&2
        return 1
    fi
    printf 'error: %s not installed (%s)\n' "$1" "$2" >&2
    exit 1
}

render() {
    src="$1"
    [ -f "$src" ] || { echo "error: no such file: $src" >&2; exit 1; }
    dst="${OUT:-${src%.*}.svg}"
    # The temp name keeps the .svg extension: d2 and mmdc pick their output
    # format from it, and would reject a plain ".tmp1234" suffix.
    tmp="${dst%.svg}.tmp$$.svg"
    CURRENT_TMP="$tmp"

    case "$src" in
        *.dot|*.gv)
            need dot "apt install graphviz" || return 0
            # Layout engine from the magic comment, defaulting to dot.
            engine="$(sed -n '1s/.*pages-site:[[:space:]]*engine=\([a-z]*\).*/\1/p' "$src")"
            command -v "${engine:=dot}" >/dev/null 2>&1 ||
                { echo "error: graphviz engine not found: $engine" >&2; exit 1; }
            "$engine" -Tsvg -o "$tmp" "$src"
            ;;
        *.msc)
            need mscgen "apt install mscgen" || return 0
            mscgen -T svg -o "$tmp" "$src"
            ;;
        *.plt|*.gnuplot)
            need gnuplot "apt install gnuplot" || return 0
            # Terminal and output are forced here so the plot script stays
            # output-agnostic and can also be rendered to PDF for print.
            gnuplot -e "set terminal svg size ${PLOT_SIZE:-800,500} font 'sans,12'; set output '$tmp'" "$src"
            ;;
        *.pic)
            # Two dialects share this extension. pikchr is the modern one and
            # rejects the .PS/.PE markers that classic pic requires, so strip
            # them and a file renders under either tool. groff only grows an
            # svg device in 1.23+, so probe rather than assume.
            if command -v pikchr >/dev/null 2>&1; then
                if ! sed '/^\.P[SE]/d' "$src" | pikchr --svg-only - > "$tmp" 2>&1; then
                    echo "error: pikchr failed on $src" >&2
                    sed 's/^/  /' "$tmp" >&2
                    rm -f "$tmp"
                    exit 1
                fi
            elif command -v groff >/dev/null 2>&1 && groff -Tsvg -p </dev/null >/dev/null 2>&1; then
                groff -p -Tsvg "$src" > "$tmp"
            else
                if [ "$KEEP_GOING" = true ]; then
                    printf 'warning: no pic renderer (install pikchr, or groff 1.23+ for -Tsvg); skipping %s\n' "$src" >&2
                    return 0
                fi
                echo "error: no pic renderer for $src. Install pikchr (https://pikchr.org)," >&2
                echo "       or groff 1.23+ which provides the svg output device." >&2
                exit 1
            fi
            ;;
        *.d2)
            # UNVERIFIED PATH: no d2 available where this was written, so the
            # invocation is kept to the smallest documented form. D2_LAYOUT can
            # select elk or tala; the default engine ships with every build.
            need d2 "https://d2lang.com, single binary" || return 0
            if [ -n "${D2_LAYOUT:-}" ]; then
                d2 --layout="$D2_LAYOUT" "$src" "$tmp"
            else
                d2 "$src" "$tmp"
            fi
            ;;
        *.mmd|*.mermaid)
            # UNVERIFIED PATH: mermaid-cli was not installable where this was
            # written, so only -i/-o and -p are used, all long-standing flags.
            need mmdc "npm install -g @mermaid-js/mermaid-cli" || return 0
            # Mermaid renders through headless Chrome. In CI and in containers
            # that needs --no-sandbox, which mmdc only accepts via a puppeteer
            # config file; point MMDC_PUPPETEER_CONFIG at one containing
            # {"args": ["--no-sandbox"]} there, and leave it unset on a desktop.
            if [ -n "${MMDC_PUPPETEER_CONFIG:-}" ]; then
                mmdc -p "$MMDC_PUPPETEER_CONFIG" -i "$src" -o "$tmp"
            else
                mmdc -i "$src" -o "$tmp"
            fi
            ;;
        *)
            echo "error: no renderer for $src" >&2
            exit 1
            ;;
    esac

    # Rename only on success so a failed render leaves the previous SVG intact
    # rather than truncating the page's figure to an empty file.
    [ -s "$tmp" ] || { rm -f "$tmp"; echo "error: renderer produced nothing for $src" >&2; exit 1; }
    mv "$tmp" "$dst"
    CURRENT_TMP=""
    printf 'diagram %s\n' "$dst" >&2
}

for f in "$@"; do
    render "$f"
done
