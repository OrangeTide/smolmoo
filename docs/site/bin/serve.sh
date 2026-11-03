#!/bin/sh
# serve.sh -- preview a built site locally, optionally rebuilding on change.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
# Usage: serve.sh [-p PORT] [-w WATCH_DIR] [-c BUILD_CMD] [DIR]
#
#   -p PORT       port to listen on            (default: 8000)
#   -w WATCH_DIR  rebuild when this tree changes (repeatable)
#   -c BUILD_CMD  command run on change        (default: make site)
#   DIR           directory to serve           (default: _site)
#
# Binds to 127.0.0.1 so a preview never becomes an accidental public server.
# Picks the first available static server; every one of them serves plain files
# with no configuration, which is exactly what Pages does in production.

set -eu

PORT=8000
DIR="_site"
BUILD_CMD="make site"
WATCH=""

while getopts 'p:w:c:h' opt; do
    case "$opt" in
        p) PORT="$OPTARG" ;;
        w) WATCH="$WATCH $OPTARG" ;;
        c) BUILD_CMD="$OPTARG" ;;
        h) sed -n '2,16p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] && DIR="$1"

[ -d "$DIR" ] || { echo "error: $DIR does not exist; build the site first" >&2; exit 1; }

serve() {
    if command -v darkhttpd >/dev/null 2>&1; then
        darkhttpd "$DIR" --port "$PORT" --addr 127.0.0.1
    elif command -v caddy >/dev/null 2>&1; then
        caddy file-server --root "$DIR" --listen 127.0.0.1:"$PORT"
    elif command -v busybox >/dev/null 2>&1; then
        busybox httpd -f -p 127.0.0.1:"$PORT" -h "$DIR"
    elif command -v python3 >/dev/null 2>&1; then
        python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$DIR"
    else
        echo "error: no static server found (install darkhttpd, caddy, or busybox)" >&2
        exit 1
    fi
}

echo "Serving $DIR at http://127.0.0.1:$PORT/  (Ctrl-C to stop)"

if [ -n "$WATCH" ] && command -v inotifywait >/dev/null 2>&1; then
    serve &
    SERVER_PID=$!
    trap 'kill $SERVER_PID 2>/dev/null || true' INT TERM EXIT
    echo "Watching:$WATCH"
    # shellcheck disable=SC2086  # $WATCH is a deliberate list of directories
    while inotifywait -qre modify,create,delete,move $WATCH >/dev/null 2>&1; do
        echo "--- change detected, rebuilding"
        sh -c "$BUILD_CMD" || echo "build failed; leaving the previous output in place"
    done
    wait $SERVER_PID
else
    [ -n "$WATCH" ] && echo "note: inotifywait not installed; serving without rebuild-on-change"
    serve
fi
