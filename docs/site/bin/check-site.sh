#!/bin/sh
# check-site.sh -- preflight a built site before it is pushed to Pages.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: check-site.sh [-q] [-x SUBDIR] [DIR]      (default DIR: _site)
#
#   -q         quiet: report problems only
#   -x SUBDIR  skip a subdirectory, repeatable. Use it for generated API docs
#              (doxygen, javadoc, rustdoc): those trees are internally
#              consistent, huge, and full of absolute paths this check would
#              flag pointlessly.
#
# Catches the failures that only show up after deploy, when the site is served
# from a subdirectory (https://user.github.io/repo/) by a plain file server:
#
#   * root-absolute links   href="/style.css" resolves to user.github.io/style.css
#   * missing .nojekyll     Jekyll eats _underscore directories
#   * dangling links        the target file is not in the output tree
#   * missing assets        img/script/link targets that were never copied
#   * localhost leftovers   preview URLs committed into templates
#
# Exit status is 1 when any error is found, 0 otherwise. Warnings do not fail.

set -eu

QUIET=false
EXCLUDE=""
while getopts 'qx:h' opt; do
    case "$opt" in
        q) QUIET=true ;;
        x) EXCLUDE="$EXCLUDE ${OPTARG%/}" ;;
        h) sed -n '2,26p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
DIR="${1:-_site}"

ERRORS=0
WARNINGS=0

# url_decode STRING -- turn %XX escapes back into bytes. Hand-rolled because
# POSIX printf has no \xNN and awk has no portable hex conversion.
url_decode() {
    printf '%s' "$1" | awk '
        BEGIN { for (i = 0; i < 16; i++) hex[sprintf("%x", i)] = hex[sprintf("%X", i)] = i }
        {
            out = ""
            n = length($0)
            for (i = 1; i <= n; i++) {
                c = substr($0, i, 1)
                if (c == "%" && i + 2 <= n) {
                    hi = substr($0, i + 1, 1); lo = substr($0, i + 2, 1)
                    if (hi in hex && lo in hex) {
                        out = out sprintf("%c", hex[hi] * 16 + hex[lo])
                        i += 2
                        continue
                    }
                }
                out = out c
            }
            printf "%s", out
        }'
}

err()  { printf 'ERROR   %s\n' "$*"; ERRORS=$((ERRORS + 1)); }
warn() { printf 'warning %s\n' "$*"; WARNINGS=$((WARNINGS + 1)); }
note() { $QUIET || printf 'ok      %s\n' "$*"; }

[ -d "$DIR" ] || { echo "ERROR   no such directory: $DIR" >&2; exit 1; }

# --- structure --------------------------------------------------------------

if [ -f "$DIR/index.html" ]; then
    note "index.html present"
else
    err "no index.html at the site root"
fi

if [ -f "$DIR/.nojekyll" ]; then
    note ".nojekyll present"
else
    warn "no .nojekyll; GitHub Pages will run Jekyll and drop _underscore paths"
fi

if [ -f "$DIR/pages.json" ]; then
    n="$(grep -c '"slug"' "$DIR/pages.json" || true)"
    if [ "$n" -gt 0 ]; then
        note "pages.json lists $n pages"
    else
        warn "pages.json is empty"
    fi
fi

# --- links and assets -------------------------------------------------------

html_files="$(find "$DIR" -name '*.html' -type f)"
[ -n "$html_files" ] || err "no HTML files in $DIR"

for f in $html_files; do
    rel="${f#"$DIR"/}"
    base="$(dirname "$f")"

    skip=false
    for ex in $EXCLUDE; do
        case "$rel" in "$ex"/*) skip=true; break ;; esac
    done
    if [ "$skip" = true ]; then continue; fi

    # Every href/src in the file, one per line. grep -o rather than sed, because
    # a sed capture takes only the last match on a line and pages routinely put
    # a link and an image on the same line.
    refs="$(grep -oE '(href|src)="[^"]*"' "$f" | sed 's/^[^"]*"//; s/"$//' | sort -u || true)"

    # Read line by line, not word by word: a filename with a space is legal and
    # word splitting would turn one link into two phantom ones. The here-doc
    # keeps the loop in this shell so the error counters survive it.
    while IFS= read -r ref; do
        case "$ref" in
            ''|'#'*|http://*|https://*|mailto:*|data:*|//*) continue ;;
            /*)
                err "$rel: root-absolute link \"$ref\" breaks on project Pages (use a relative path)"
                continue
                ;;
        esac

        target="${ref%%#*}"
        target="${target%%\?*}"
        [ -z "$target" ] && continue

        # Markdown converters percent-encode spaces and other characters in
        # link targets, so the href rarely matches the filename byte for byte.
        # Compare against the decoded form or every such asset reads as missing.
        target="$(url_decode "$target")"

        path="$base/$target"
        if [ -d "$path" ]; then
            [ -f "$path/index.html" ] || err "$rel: \"$ref\" is a directory with no index.html"
        elif [ ! -e "$path" ]; then
            err "$rel: \"$ref\" does not exist in the output"
        fi
    done <<REFS
$refs
REFS

    if grep -q 'localhost\|127\.0\.0\.1' "$f"; then
        warn "$rel: contains a localhost URL"
    fi

    if grep -q '{{[A-Z_]*}}' "$f"; then
        err "$rel: unreplaced template placeholder $(grep -o '{{[A-Z_]*}}' "$f" | sort -u | tr '\n' ' ')"
    fi
done

# --- report -----------------------------------------------------------------

printf '\n%s: %d error(s), %d warning(s)\n' "$DIR" "$ERRORS" "$WARNINGS"
[ "$ERRORS" -eq 0 ]
