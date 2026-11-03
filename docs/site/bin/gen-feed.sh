#!/bin/sh
# gen-feed.sh -- write an RSS 2.0 feed from OUT/pages.json.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: SITE_URL=https://user.github.io/repo gen-feed.sh [-o OUT] [-n N]
#
#   -o OUT   output directory (default: _site)
#   -n N     limit to the N newest pages (default: 20)
#
# SITE_URL must be the deployed base URL with no trailing slash. Feed readers
# resolve relative links inconsistently, so the feed is the one place in the
# site that needs absolute URLs.

set -eu

DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
. "$DIR/site-lib.sh"

OUT="_site"
LIMIT=20

while getopts 'o:n:h' opt; do
    case "$opt" in
        o) OUT="$OPTARG" ;;
        n) LIMIT="$OPTARG" ;;
        h) sed -n '2,15p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done

: "${SITE_URL:?set SITE_URL to the deployed base URL, e.g. https://user.github.io/repo}"
SITE_URL="${SITE_URL%/}"

PAGES_JSON="$OUT/pages.json"
[ -f "$PAGES_JSON" ] || { echo "error: $PAGES_JSON not found; run build-site.sh first" >&2; exit 1; }

# Raw values; each use site wraps them in html_escape for XML.
field() { json_field "$1" "$2"; }

{
    cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom">
  <channel>
    <title>$(html_escape "${SITE_TITLE:-Site}")</title>
    <link>$SITE_URL/</link>
    <description>$(html_escape "${SITE_TAGLINE:-}")</description>
    <language>${SITE_LANG:-en-us}</language>
    <atom:link href="$SITE_URL/feed.xml" rel="self" type="application/rss+xml"/>
EOF

    grep '"slug"' "$PAGES_JSON" | head -n "$LIMIT" | while IFS= read -r line; do
        # Escaped even though slugs are tame: <link> is XML, and one
        # ampersand in a slug makes the whole feed unparseable.
        url="$(html_escape "$SITE_URL/$(field url "$line")")"
        cat <<EOF
    <item>
      <title>$(html_escape "$(field title "$line")")</title>
      <link>$url</link>
      <guid isPermaLink="true">$url</guid>
      <pubDate>$(rfc_date "$(field date "$line")")</pubDate>
      <description>$(html_escape "$(field abstract "$line")")</description>
      <category>$(html_escape "$(field category "$line")")</category>
    </item>
EOF
    done

    printf '  </channel>\n</rss>\n'
} > "$OUT/feed.xml"

say "==> $OUT/feed.xml written"
