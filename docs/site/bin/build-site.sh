#!/bin/sh
# build-site.sh -- render a directory of Markdown into a static site.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: build-site.sh [-o OUT] [-t TEMPLATES] [-s STATIC] [-a] [-u] [SRC]
#
#   -o OUT        output directory            (default: _site)
#   -t TEMPLATES  template directory          (default: site/templates)
#   -s STATIC     static asset directory      (default: site/static)
#   -a            include drafts
#   -u            update in place, skip the initial clean
#   SRC           content directory           (default: content)
#
# Two source layouts are recognised, and they can be mixed:
#
#   content/topic.md            -> OUT/topic/index.html
#   content/topic/index.md      -> OUT/topic/index.html, with every sibling
#                                  file copied alongside it (images, data,
#                                  downloads) and sibling .md files rendered
#                                  as their own pages
#
# Side effect: writes OUT/pages.json, the metadata index that gen-index.sh and
# gen-feed.sh consume. Keeping page rendering and landing-page rendering in
# separate scripts is what lets a project swap in a hand-designed landing page
# without touching the page builder.

set -eu

DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
. "$DIR/site-lib.sh"

OUT="_site"
TEMPLATES="site/templates"
STATIC="site/static"
ALL=false
UPDATE=false

while getopts 'o:t:s:auh' opt; do
    case "$opt" in
        o) OUT="$OPTARG" ;;
        t) TEMPLATES="$OPTARG" ;;
        s) STATIC="$OPTARG" ;;
        a) ALL=true ;;
        u) UPDATE=true ;;
        h) sed -n '2,30p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
SRC="${1:-content}"

[ -d "$SRC" ] || { echo "error: no content directory: $SRC" >&2; exit 1; }
[ -f "$TEMPLATES/page.html" ] || { echo "error: missing $TEMPLATES/page.html" >&2; exit 1; }

$UPDATE || rm -rf "$OUT"
mkdir -p "$OUT"

# GitHub Pages runs Jekyll unless told not to, and Jekyll silently drops files
# and directories whose names start with an underscore.
: > "$OUT/.nojekyll"

if [ -d "$STATIC" ]; then
    mkdir -p "$OUT/static"
    cp -R "$STATIC/." "$OUT/static/"
fi

say "==> engine: $(pick_engine)"

# ---------------------------------------------------------------------------
# Render one markdown file into OUT/<slug>/index.html
# ---------------------------------------------------------------------------

# root_prefix SLUG -- the relative path from that page back to the site root.
# Pages nested under a topic directory need "../.." rather than "..", and
# getting this wrong breaks the stylesheet only on the deep pages, which is
# exactly the kind of bug that survives a quick look at the front page.
root_prefix() {
    _depth="$(printf '%s' "$1" | tr -cd '/' | wc -c | tr -d ' ')"
    _p=".."
    while [ "$_depth" -gt 0 ]; do
        _p="$_p/.."
        _depth=$((_depth - 1))
    done
    printf '%s' "$_p"
}
# render_page SRC_MD SLUG ROOT_PREFIX
render_page() {
    _md="$1"; _slug="$2"; _root="$3"
    _dest="$OUT/$_slug/index.html"
    mkdir -p "$(dirname "$_dest")"

    _title="$(fm_get title "$_md")"
    [ -n "$_title" ] || _title="$(first_h1 "$_md")"
    [ -n "$_title" ] || _title="$_slug"
    _date="$(fm_get date "$_md")"
    _revised="$(fm_get revised "$_md")"
    _abstract="$(fm_get abstract "$_md")"
    _category="$(fm_get category "$_md")"
    _tags="$(fm_get tags "$_md")"

    _revised_html=""
    [ -n "$_revised" ] && _revised_html="<span class=\"page-revised\">Revised <time datetime=\"$_revised\">$(fmt_date "$_revised")</time></span>"

    _body="$(md_to_html "$_md")"

    render_template "$TEMPLATES/page.html" \
        TITLE "$(html_escape "$_title")" \
        SITE_TITLE "${SITE_TITLE:-Site}" \
        DATE "$_date" \
        DATE_DISPLAY "$(fmt_date "$_date")" \
        REVISED "$_revised_html" \
        CATEGORY "$(html_escape "$_category")" \
        TAGS "$(html_escape "$_tags")" \
        ABSTRACT "$(html_escape "$_abstract")" \
        ROOT "$_root" \
        BODY "$_body" > "$_dest"

    # The TOC is built from the rendered HTML rather than the Markdown so the
    # heading ids match whatever the engine actually emitted.
    render_template "$_dest" TOC "$(make_toc "$_dest")" > "$_dest.tmp"
    mv "$_dest.tmp" "$_dest"

    say "  page   $_slug"
}

# ---------------------------------------------------------------------------
# Walk the content tree
# ---------------------------------------------------------------------------

PAGES=""   # newline-separated "sortkey<TAB>json"

add_record() {
    # add_record SLUG MD_FILE
    _slug="$1"; _md="$2"
    _title="$(fm_get title "$_md")"; [ -n "$_title" ] || _title="$(first_h1 "$_md")"
    [ -n "$_title" ] || _title="$_slug"
    _date="$(fm_get date "$_md")"
    _weight="$(fm_get weight "$_md")"
    _cover="$(fm_get cover "$_md")"
    [ -n "$_cover" ] && _cover="$_slug/$_cover"
    _json="{\"slug\":\"$(json_escape "$_slug")\",\"url\":\"$_slug/\",\"title\":\"$(json_escape "$_title")\""
    _json="$_json,\"date\":\"$(json_escape "$_date")\",\"dateDisplay\":\"$(json_escape "$(fmt_date "$_date")")\""
    _json="$_json,\"revised\":\"$(json_escape "$(fm_get revised "$_md")")\""
    _json="$_json,\"abstract\":\"$(json_escape "$(fm_get abstract "$_md")")\""
    _json="$_json,\"category\":\"$(json_escape "$(fm_get category "$_md")")\""
    _json="$_json,\"tags\":\"$(json_escape "$(fm_get tags "$_md")")\""
    _json="$_json,\"cover\":\"$(json_escape "$_cover")\""
    _json="$_json,\"weight\":\"$(json_escape "$_weight")\"}"
    # Sort key: date descending is the common case for article sites, so pages
    # with no date sort last rather than first.
    PAGES="$PAGES
${_date:-0000-00-00}	$_json"
}

skip_draft() {
    # skip_draft MD -- true when the page is a draft and drafts are excluded
    $ALL && return 1
    case "$(fm_get draft "$1")" in
        true|yes|1) return 0 ;;
    esac
    case "$(fm_get published "$1")" in
        false|no|0) return 0 ;;
    esac
    return 1
}

# Layout 1: content/<slug>/index.md (with companion files)
for index_md in "$SRC"/*/index.md; do
    [ -f "$index_md" ] || continue
    dir="$(dirname "$index_md")"
    slug="$(basename "$dir")"
    if skip_draft "$index_md"; then say "  draft  $slug (skipped)"; continue; fi

    # Companion files: everything that is not markdown, preserving subdirs.
    (cd "$dir" && find . -type f ! -name '*.md' ! -name '.gitignore') |
    while IFS= read -r rel; do
        rel="${rel#./}"
        mkdir -p "$OUT/$slug/$(dirname "$rel")"
        cp "$dir/$rel" "$OUT/$slug/$rel"
    done

    render_page "$index_md" "$slug" ".."
    add_record "$slug" "$index_md"

    # Companion markdown (a demo README, an appendix) becomes its own page,
    # at any depth, keeping the source tree's shape in the output.
    find "$dir" -name '*.md' -type f ! -path "$index_md" | while IFS= read -r extra; do
        rel="${extra#"$dir"/}"
        # index.md and README.md become the directory's own page, so a link to
        # "demo/" resolves the way the author wrote it and the way GitHub
        # renders the same tree.
        case "$(basename "$rel")" in
            index.md|README.md) page_slug="$slug/$(dirname "$rel")" ;;
            *)                  page_slug="$slug/${rel%.md}" ;;
        esac
        page_slug="$(printf '%s' "$page_slug" | sed 's|/\./|/|g; s|/$||')"
        render_page "$extra" "$page_slug" "$(root_prefix "$page_slug")"
    done
done

# Layout 2: content/<slug>.md
for md in "$SRC"/*.md; do
    [ -f "$md" ] || continue
    slug="$(basename "$md" .md)"
    if [ "$slug" = "index" ]; then
        say "  note   $SRC/index.md skipped; the landing page comes from gen-index.sh"
        continue
    fi
    if skip_draft "$md"; then say "  draft  $slug (skipped)"; continue; fi
    render_page "$md" "$slug" ".."
    add_record "$slug" "$md"
done

# ---------------------------------------------------------------------------
# pages.json -- newest first, then by weight for undated pages
# ---------------------------------------------------------------------------

{
    printf '[\n'
    printf '%s\n' "$PAGES" | grep '	' | sort -t'	' -k1,1r | cut -f2- |
        awk '{ printf "%s  %s", (NR > 1 ? ",\n" : ""), $0 } END { if (NR) printf "\n" }'
    printf ']\n'
} > "$OUT/pages.json"

say "==> $OUT ready ($(grep -c '"slug"' "$OUT/pages.json" || echo 0) pages)"
