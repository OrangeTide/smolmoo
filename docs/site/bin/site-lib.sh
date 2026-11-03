# shellcheck shell=sh
# site-lib.sh -- shared helpers for the pages-site build scripts.
# Source this file, do not execute it.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)

# ---------------------------------------------------------------------------
# Frontmatter
# ---------------------------------------------------------------------------

# fm_get FIELD FILE
# Print one top-level scalar field from the YAML frontmatter block, or nothing.
# Quotes are stripped. An inline [a, b] list is printed without the brackets.
fm_get() {
    awk -v key="$1" '
        NR == 1 && $0 ~ /^---[[:space:]]*$/ { inside = 1; next }
        inside && $0 ~ /^(---|\.\.\.)[[:space:]]*$/ { exit }
        inside && $0 ~ /^[^[:space:]#]/ {
            idx = index($0, ":")
            if (idx == 0) next
            k = substr($0, 1, idx - 1)
            v = substr($0, idx + 1)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", k)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
            if (k != key) next
            if (v ~ /^".*"$/ || v ~ /^'"'"'.*'"'"'$/) v = substr(v, 2, length(v) - 2)
            else if (v ~ /^\[.*\]$/) v = substr(v, 2, length(v) - 2)
            print v
            exit
        }
    ' "$2"
}

# yaml_get FIELD FILE
# Same shape as fm_get, but for a standalone YAML file (metadata.yaml,
# meta.yaml) where the whole file is the mapping and the --- fence is optional.
# Missing files return empty rather than failing, so callers can probe for an
# optional per-book override without guarding every call.
yaml_get() {
    [ -f "$2" ] || return 0
    awk -v key="$1" '
        $0 ~ /^(---|\.\.\.)[[:space:]]*$/ { next }
        /^[^[:space:]#]/ {
            idx = index($0, ":")
            if (idx == 0) next
            k = substr($0, 1, idx - 1)
            v = substr($0, idx + 1)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", k)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
            if (k != key) next
            if (v ~ /^".*"$/ || v ~ /^'"'"'.*'"'"'$/) v = substr(v, 2, length(v) - 2)
            print v
            exit
        }
    ' "$2"
}

# fm_has FILE -- true when the file opens with a frontmatter block.
fm_has() {
    head -1 "$1" 2>/dev/null | grep -q '^---[[:space:]]*$'
}

# first_h1 FILE -- the first "# Heading" line, without the marker.
first_h1() {
    grep -m1 '^#[[:space:]]' "$1" 2>/dev/null | sed 's/^#[[:space:]]*//'
}

# ---------------------------------------------------------------------------
# Reading back pages.json
# ---------------------------------------------------------------------------

# json_field NAME RECORD -- read one string field from a single-line JSON
# object, honouring backslash escapes, and return the decoded value.
#
# A sed capture cannot do this. `"title":"\([^"]*\)"` stops at the first quote
# inside the value, so a title containing \" is silently truncated, and the
# breakage only shows up on the one page whose title has a quote in it. This
# scans character by character instead.
json_field() {
    JF_NAME="$1"; export JF_NAME
    printf '%s' "$2" | awk '
        BEGIN { key = "\"" ENVIRON["JF_NAME"] "\":\"" }
        {
            start = index($0, key)
            if (start == 0) exit
            i = start + length(key)
            out = ""
            n = length($0)
            while (i <= n) {
                c = substr($0, i, 1)
                if (c == "\\") {
                    e = substr($0, i + 1, 1)
                    if (e == "n") out = out "\n"
                    else if (e == "t") out = out "\t"
                    else if (e == "r") out = out "\r"
                    else out = out e          # \" and \\ decode to themselves
                    i += 2
                    continue
                }
                if (c == "\"") break          # unescaped quote ends the value
                out = out c
                i++
            }
            printf "%s", out
        }'
    unset JF_NAME
}

# ---------------------------------------------------------------------------
# Escaping
# ---------------------------------------------------------------------------

html_escape() {
    printf '%s' "$1" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g; s/"/\&quot;/g'
}

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/\\t/g' | tr -d '\r' | awk '
        { lines[NR] = $0 }
        END { for (i = 1; i <= NR; i++) printf "%s%s", (i > 1 ? "\\n" : ""), lines[i] }
    '
}

# ---------------------------------------------------------------------------
# Templating
# ---------------------------------------------------------------------------

# render_template TEMPLATE_FILE KEY VALUE [KEY VALUE ...]
# Substitutes {{KEY}} placeholders and writes the result to stdout. Values may
# contain newlines, slashes, and backslashes, which is why this is awk and not
# sed. Unreplaced placeholders are left alone so a partial render can be piped
# through render_template again.
render_template() {
    _tpl="$1"; shift
    _buf="$(cat "$_tpl")"
    while [ $# -ge 2 ]; do
        _RT_KEY="{{$1}}"
        _RT_VAL="$2"
        export _RT_KEY _RT_VAL
        _buf="$(printf '%s\n' "$_buf" | awk '
            BEGIN { key = ENVIRON["_RT_KEY"]; val = ENVIRON["_RT_VAL"] }
            {
                out = ""
                line = $0
                while ((idx = index(line, key)) > 0) {
                    out = out substr(line, 1, idx - 1) val
                    line = substr(line, idx + length(key))
                }
                print out line
            }
        ')"
        unset _RT_KEY _RT_VAL
        shift 2
    done
    printf '%s\n' "$_buf"
}

# ---------------------------------------------------------------------------
# Markdown
# ---------------------------------------------------------------------------

# pick_engine -- echo the markdown engine to use: lowdown or pandoc.
# Honours $SITE_ENGINE when set, otherwise prefers lowdown (much faster for
# HTML-only sites) and falls back to pandoc.
pick_engine() {
    case "${SITE_ENGINE:-auto}" in
        lowdown|pandoc) printf '%s' "$SITE_ENGINE"; return 0 ;;
    esac
    if command -v lowdown >/dev/null 2>&1; then
        printf 'lowdown'
    elif command -v pandoc >/dev/null 2>&1; then
        printf 'pandoc'
    else
        echo "error: neither lowdown nor pandoc is installed" >&2
        return 1
    fi
}

# md_to_html FILE -- markdown body (frontmatter stripped) as an HTML fragment.
md_to_html() {
    case "$(pick_engine)" in
        lowdown)
            lowdown --parse-metadata -thtml \
                --html-no-escapehtml --html-no-skiphtml "$1"
            ;;
        pandoc)
            pandoc -f markdown -t html --no-highlight "$1"
            ;;
    esac
}

# make_toc HTML_FILE [MIN_HEADINGS]
# Emit a collapsible table of contents built from the <h2 id="..."> tags of an
# already-rendered page. Prints nothing when the page has too few headings for
# a TOC to earn its space.
make_toc() {
    _min="${2:-3}"
    _n="$(grep -c '<h2 id="' "$1" 2>/dev/null || true)"
    [ "${_n:-0}" -lt "$_min" ] && return 0
    printf '<input type="checkbox" id="toc-toggle" class="toc-state">'
    printf '<label for="toc-toggle" class="toc-btn">Contents</label>'
    printf '<nav class="toc-panel"><div class="toc-header"><span>Contents</span>'
    printf '<label for="toc-toggle" class="toc-close">&times;</label></div><ul class="toc-list">'
    sed -n 's|.*<h2 id="\([^"]*\)"[^>]*>\(.*\)</h2>.*|<li><a href="#\1">\2</a></li>|p' "$1" | tr -d '\n'
    printf '</ul></nav>'
}

# ---------------------------------------------------------------------------
# Misc
# ---------------------------------------------------------------------------

# fmt_date ISO_DATE -- "July 25, 2026" where date(1) cooperates, else unchanged.
fmt_date() {
    [ -n "$1" ] || return 0
    date -d "$1" '+%B %-d, %Y' 2>/dev/null || printf '%s' "$1"
}

# rfc_date ISO_DATE -- RFC 2822 form for RSS, else unchanged.
rfc_date() {
    [ -n "$1" ] || return 0
    date -d "$1" -R 2>/dev/null || printf '%s' "$1"
}

# slugify TEXT -- lowercase, dashes, no punctuation.
slugify() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' \
        | sed 's/[^a-z0-9]\+/-/g; s/^-//; s/-$//'
}

# say MESSAGE -- build progress on stderr, so stdout stays pipeable.
say() { printf '%s\n' "$*" >&2; }
