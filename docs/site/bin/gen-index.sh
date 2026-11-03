#!/bin/sh
# gen-index.sh -- render the landing page from OUT/pages.json.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: gen-index.sh [-o OUT] [-t TEMPLATES] [-m list|cards|data]
#
#   -m list    one <li> per page: title, date, abstract      (default)
#   -m cards   a grid of cards with cover images
#   -m data    write OUT/pages.js only (window.PAGES = [...]) and leave
#              index.html alone -- use this when the landing page is a
#              hand-designed HTML file that renders the list in JavaScript
#
# The template is TEMPLATES/index.html with {{SITE_TITLE}}, {{TAGLINE}} and
# {{ITEMS}} placeholders. Everything else in that file is yours: this script
# only fills the slot, which is why a heavily designed landing page and this
# generator can coexist.

set -eu

DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
. "$DIR/site-lib.sh"

OUT="_site"
TEMPLATES="site/templates"
MODE="list"

while getopts 'o:t:m:h' opt; do
    case "$opt" in
        o) OUT="$OPTARG" ;;
        t) TEMPLATES="$OPTARG" ;;
        m) MODE="$OPTARG" ;;
        h) sed -n '2,20p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done

PAGES_JSON="$OUT/pages.json"
[ -f "$PAGES_JSON" ] || { echo "error: $PAGES_JSON not found; run build-site.sh first" >&2; exit 1; }

# field NAME LINE -- one field from a record, escaped for HTML.
# Everything here lands inside markup, so escaping is not optional: an abstract
# containing <script> or an ampersand would otherwise be injected verbatim into
# the index page.
field() {
    html_escape "$(json_field "$1" "$2")"
}

records() {
    grep '"slug"' "$PAGES_JSON"
}

if [ "$MODE" = data ]; then
    { printf 'window.PAGES = '; cat "$PAGES_JSON"; printf ';\n'; } > "$OUT/pages.js"
    say "==> $OUT/pages.js written ($(records | wc -l | tr -d ' ') pages)"
    exit 0
fi

[ -f "$TEMPLATES/index.html" ] || { echo "error: missing $TEMPLATES/index.html" >&2; exit 1; }

ITEMS=""
while IFS= read -r line; do
    url="$(field url "$line")"
    title="$(field title "$line")"
    date="$(field date "$line")"
    disp="$(field dateDisplay "$line")"
    abstract="$(field abstract "$line")"
    category="$(field category "$line")"
    cover="$(field cover "$line")"

    case "$MODE" in
        cards)
            img=""
            [ -n "$cover" ] && img="<img class=\"card-cover\" src=\"$cover\" alt=\"\" loading=\"lazy\">"
            ITEMS="$ITEMS
  <a class=\"card\" href=\"$url\">
    $img
    <span class=\"card-category\">$category</span>
    <h2 class=\"card-title\">$title</h2>
    <time class=\"card-date\" datetime=\"$date\">$disp</time>
    <p class=\"card-abstract\">$abstract</p>
  </a>"
            ;;
        list)
            ITEMS="$ITEMS
  <li class=\"entry\">
    <a class=\"entry-title\" href=\"$url\">$title</a>
    <time class=\"entry-date\" datetime=\"$date\">$disp</time>
    <p class=\"entry-abstract\">$abstract</p>
  </li>"
            ;;
        *)
            echo "error: unknown mode: $MODE" >&2; exit 2 ;;
    esac
done <<EOF
$(records)
EOF

render_template "$TEMPLATES/index.html" \
    SITE_TITLE "${SITE_TITLE:-Site}" \
    TAGLINE "${SITE_TAGLINE:-}" \
    FEED_HEAD "<link rel=\"alternate\" type=\"application/rss+xml\" title=\"${SITE_TITLE:-Site}\" href=\"feed.xml\">" \
    FEED_LINK '<a href="feed.xml">RSS</a>' \
    ITEMS "$ITEMS" > "$OUT/index.html"

say "==> $OUT/index.html written ($(records | wc -l | tr -d ' ') entries, $MODE)"
