#!/bin/sh
# gen-downloads.sh -- landing page for a multi-format download site.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: gen-downloads.sh [-o OUT] [-t TEMPLATES] [-f "html pdf epub md"] NAME...
#
#   -o OUT        built site directory     (default: _site)
#   -t TEMPLATES  template directory       (default: site/templates)
#   -f FORMATS    formats to offer, in display order (default: html pdf epub md)
#   NAME...       one per book/document; OUT/NAME.<fmt> is linked when present
#
# A format whose file is missing is silently left out of the card rather than
# linked and broken. That is what lets CI build EPUB and HTML without typst and
# still publish a coherent page, and it keeps a half-finished local build
# previewable.
#
# Title for each card: books/NAME/meta.yaml, then metadata.yaml when there is
# only one book, then the first chapter's "# Heading", then NAME.

set -eu

DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
. "$DIR/site-lib.sh"

OUT="_site"
TEMPLATES="site/templates"
FORMATS="html pdf epub md"

while getopts 'o:t:f:h' opt; do
    case "$opt" in
        o) OUT="$OPTARG" ;;
        t) TEMPLATES="$OPTARG" ;;
        f) FORMATS="$OPTARG" ;;
        h) sed -n '2,20p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { echo "usage: gen-downloads.sh [options] NAME..." >&2; exit 2; }
[ -f "$TEMPLATES/index.html" ] || { echo "error: missing $TEMPLATES/index.html" >&2; exit 1; }

label_for() {
    case "$1" in
        pdf)  printf 'PDF' ;;
        epub) printf 'EPUB' ;;
        html) printf 'Read online' ;;
        odt)  printf 'ODT' ;;
        md)   printf 'Markdown' ;;
        *)    printf '%s' "$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')" ;;
    esac
}

ITEMS=""
for name in "$@"; do
    # Title priority: the book's own pandoc metadata, then the shared metadata
    # file when this is the only book, then the first chapter's H1. A shared
    # metadata title would be wrong for every book but the first, which is why
    # it only applies to a single-book site.
    title="$(yaml_get title "books/$name/meta.yaml")"
    if [ -z "$title" ] && [ $# -eq 1 ]; then
        title="$(yaml_get title metadata.yaml)"
    fi
    if [ -z "$title" ]; then
        for src in books/"$name"/*.md; do
            [ -f "$src" ] || continue
            h1="$(first_h1 "$src")"
            [ -n "$h1" ] && { title="$h1"; break; }
        done
    fi
    [ -n "$title" ] || title="$name"

    links=""
    for fmt in $FORMATS; do
        file="$name.$fmt"
        [ -f "$OUT/$file" ] || continue
        size="$(du -h "$OUT/$file" 2>/dev/null | cut -f1)"
        links="$links
      <a class=\"dl\" href=\"$file\">$(label_for "$fmt") <span class=\"dl-size\">$size</span></a>"
    done

    [ -n "$links" ] || { say "  skip   $name (nothing built)"; continue; }

    cover=""
    [ -f "$OUT/$name-cover.png" ] &&
        cover="<img class=\"card-cover\" src=\"$name-cover.png\" alt=\"\" loading=\"lazy\">"

    ITEMS="$ITEMS
  <li class=\"card download-card\">
    $cover
    <h2 class=\"card-title\">$(html_escape "$title")</h2>
    <p class=\"dl-links\">$links
    </p>
  </li>"
    say "  card   $name"
done

# A download page has no feed: the entries are documents, not dated posts.
# Emptying the placeholders keeps the shared template usable for both.
render_template "$TEMPLATES/index.html" \
    SITE_TITLE "${SITE_TITLE:-Downloads}" \
    TAGLINE "${SITE_TAGLINE:-}" \
    FEED_HEAD "" \
    FEED_LINK "" \
    ITEMS "$ITEMS" > "$OUT/index.html"

say "==> $OUT/index.html written"
