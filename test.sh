#!/bin/sh
# test.sh : smoke test for smolmoo
set -e

PORT=7778
PASS=0
FAIL=0
P1= P2= P3= SPID=

fail() { echo "FAIL: $1"; FAIL=$((FAIL + 1)); }
pass() { echo "ok: $1"; PASS=$((PASS + 1)); }
check() { if "$@"; then pass "$1"; else fail "$1"; fi; }

# waitgrep FILE PATTERN : poll FILE for PATTERN up to ~3s.
# SSE output is asynchronous, so a fixed sleep races the stream. Retry
# instead: return as soon as the line appears, give up after the timeout.
waitgrep() {
	_i=0
	while [ $_i -lt 60 ]; do
		grep -q "$2" "$1" 2>/dev/null && return 0
		sleep 0.05
		_i=$((_i + 1))
	done
	return 1
}

# check_log FILE PATTERN LABEL : waitgrep then report pass/fail.
check_log() {
	if waitgrep "$1" "$2"; then pass "$3"; else fail "$3"; fi
}

# fight_over FOE... : drive the current room fight to a confirmed teardown.
# Attacks the named foes to Down them, then probes with `hold`, which replies
# "not in a fight" only once __combat has cleared cb_active. Loops until that
# fresh reply appears (counted as a delta so stale matches never trigger it),
# so teardown is deterministic regardless of dice or turn timing. A fixed
# attack count cannot guarantee a tough foe Downs, which is what let a fight
# leak into the next scenario and race its opening. Because each attack runs
# before the probe and consumes any player turn, `hold` never reports
# "holds, watching" here, so it does not pollute later assertions.
fight_over() {
	_k=0
	while [ $_k -lt 40 ]; do
		_b=$(grep -c 'not in a fight' /tmp/smolmoo_p1.log 2>/dev/null || true)
		for _f in "$@"; do
			curl -sf -X POST -d "$SID1 attack $_f" \
				http://localhost:$PORT/cmd >/dev/null
		done
		curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
		_j=0
		while [ $_j -lt 20 ]; do
			_a=$(grep -c 'not in a fight' /tmp/smolmoo_p1.log 2>/dev/null || true)
			[ "${_a:-0}" -gt "${_b:-0}" ] && return 0
			sleep 0.05
			_j=$((_j + 1))
		done
		_k=$((_k + 1))
	done
	return 1
}

# social_drive CMD OUTCOME : drive a social scene by sending "$SID1 CMD" and,
# after each send, polling for a fresh OUTCOME line before sending again -- the
# same send-then-poll shape fight_over uses. That ordering matters: a social
# scene's yield clears cb_active synchronously, so the very next command would
# open a brand-new scene. Polling for the outcome before resending means the
# command that ends the scene is never chased by a stray one that reopens it,
# which is what let earlier scenes bleed their targets into the next. OUTCOME is
# counted as a delta so a matching line from an earlier scene never trips it.
# Returns 0 once a fresh OUTCOME appears, 1 if the budget runs out.
social_drive() {
	_k=0
	while [ $_k -lt 40 ]; do
		_b=$(grep -c "$2" /tmp/smolmoo_p1.log 2>/dev/null || true)
		curl -sf -X POST -d "$SID1 $1" http://localhost:$PORT/cmd >/dev/null
		_j=0
		while [ $_j -lt 20 ]; do
			_a=$(grep -c "$2" /tmp/smolmoo_p1.log 2>/dev/null || true)
			[ "${_a:-0}" -gt "${_b:-0}" ] && return 0
			sleep 0.05
			_j=$((_j + 1))
		done
		_k=$((_k + 1))
	done
	return 1
}

DEPOT=$(mktemp -d)
cp -a depot/* "$DEPOT/" 2>/dev/null || true

CDEP=$(mktemp -d)

cleanup() {
	kill $P1 $P2 $P3 $SPID 2>/dev/null
	wait 2>/dev/null
	rm -rf /tmp/smolmoo_p1.log /tmp/smolmoo_p2.log /tmp/smolmoo_p3.log \
		/tmp/smolmoo_err.log /tmp/smolmoo_exp.txt /tmp/smolmoo_m2.txt \
		/tmp/smolmoo_mrg.txt "$DEPOT" "$CDEP"
}
trap cleanup EXIT

SMOLMOO_PORT=$PORT SMOLMOO_DEPOT="$DEPOT" SMOLMOO_DEATH_MS=6000 _build/smolmoo serve --bootstrap 2>/tmp/smolmoo_err.log &
SPID=$!

# extract admin invite code from bootstrap output (waits for server boot)
waitgrep /tmp/smolmoo_err.log 'admin invite:' || true
INVITE=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_err.log | head -1 | cut -d' ' -f3)
[ -n "$INVITE" ] && pass "bootstrap invite" || fail "bootstrap invite"

# GET / serves HTML
curl -sf http://localhost:$PORT/ | grep -q '<pre' \
	&& pass "GET /" || fail "GET /"

# connect two SSE streams
curl -sN http://localhost:$PORT/events > /tmp/smolmoo_p1.log &
P1=$!
curl -sN http://localhost:$PORT/events > /tmp/smolmoo_p2.log &
P2=$!
waitgrep /tmp/smolmoo_p1.log "^data: I" || true
waitgrep /tmp/smolmoo_p2.log "^data: I" || true

# extract session IDs
SID1=$(grep -m1 "^data: I" /tmp/smolmoo_p1.log | sed 's/^data: I//')
SID2=$(grep -m1 "^data: I" /tmp/smolmoo_p2.log | sed 's/^data: I//')
[ -n "$SID1" ] && pass "session 1 ($SID1)" || fail "session 1"
[ -n "$SID2" ] && pass "session 2 ($SID2)" || fail "session 2"

# create accounts using admin invite
curl -sf -X POST -d "$SID1 create TestPlayer1 pass1 $INVITE" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'data: +' "create acct 1"

curl -sf -X POST -d "$SID2 create TestPlayer2 pass2 $INVITE" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'data: +' "create acct 2"

# player 1 says something
curl -sf -X POST -d "$SID1 say hello world" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You say "hello world"' "say echo"
check_log /tmp/smolmoo_p2.log 'says "hello world"' "say broadcast"

# look
curl -sf -X POST -d "$SID1 look" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "The Lobby" "look"

# help: the index lists topics, and a named topic prints its entry (both come
# from the #0.help object seeded in world.data, see help.md).
curl -sf -X POST -d "$SID1 help" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "Help topics" "help lists topics"
curl -sf -X POST -d "$SID1 help emote" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "Describe an action to the room" "help prints a topic entry"
curl -sf -X POST -d "$SID1 help nosuchtopic" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "No help on that topic" "help rejects an unknown topic"

# say shorthand "
curl -sf -X POST -d "$SID1 \"hi there" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You say "hi there"' "say shorthand"

# emote
curl -sf -X POST -d "$SID1 :waves" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "TestPlayer1 waves" "emote"
check_log /tmp/smolmoo_p2.log "TestPlayer1 waves" "emote broadcast"

# emote nospace ::
curl -sf -X POST -d "$SID1 ::'s sword glows" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "TestPlayer1's sword glows" "emote nospace"

# whisper
curl -sf -X POST -d "$SID1 whisper TestPlayer2 secret msg" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'whispers, "secret msg"' "whisper recv"
check_log /tmp/smolmoo_p1.log 'You whisper, "secret msg"' "whisper send"

# web editor: GET /edit serves editor page
curl -sf "http://localhost:$PORT/edit?obj=101&prop=name&sid=$SID1" | grep -q 'textarea' \
	&& pass "GET /edit" || fail "GET /edit"

# web editor: GET /view serves editor page
curl -sf "http://localhost:$PORT/view?obj=101&prop=name&sid=$SID1" | grep -q 'textarea' \
	&& pass "GET /view" || fail "GET /view"

# web editor: GET /prop reads property
PVAL=$(curl -sf "http://localhost:$PORT/prop?obj=101&prop=name&sid=$SID1")
[ "$PVAL" = "The Lobby" ] && pass "GET /prop" || fail "GET /prop"

# web editor: POST /prop updates property
curl -sf -X POST -d "New Lobby Name" \
	"http://localhost:$PORT/prop?obj=101&prop=name&sid=$SID1" | grep -q 'OK' \
	&& pass "POST /prop" || fail "POST /prop"

# web editor: verify property was updated
PVAL2=$(curl -sf "http://localhost:$PORT/prop?obj=101&prop=name&sid=$SID1")
[ "$PVAL2" = "New Lobby Name" ] && pass "prop updated" || fail "prop updated"

# web editor: invalid session returns 403
curl -sf "http://localhost:$PORT/prop?obj=101&prop=name&sid=99" >/dev/null \
	&& fail "invalid sid" || pass "invalid sid"

# web editor: @edit sends SSE E command
curl -sf -X POST -d "$SID1 @edit #101.name" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'data: E/edit' "@edit SSE"

# web editor: @view sends SSE V command
curl -sf -X POST -d "$SID1 @view #101.name" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'data: V/view' "@view SSE"

# --- M19: typed slot matching ---

# <obj> slot: resolve object by name
curl -sf -X POST -d "$SID1 testget Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'DOBJ=Rusty Sword' "slot <obj>"

# <obj> in <obj>: two objects with atom preposition
curl -sf -X POST -d "$SID1 testput Rusty Sword in Red Potion" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'DOBJ=Rusty Sword PREP=in IOBJ=Red Potion' "slot <obj> in <obj>"

# <obj> <atom=to|at> <player>: object + atom + player
curl -sf -X POST -d "$SID1 testgive Rusty Sword to TestPlayer2" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'DOBJ=Rusty Sword PREP=to IOBJ=TestPlayer2' "slot <obj> atom <player>"

# --- M19: sys_setprop ---

# admin sets property via verb
curl -sf -X POST -d "$SID1 testsetprop Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'SETPROP:OK' "sys_setprop ok"

# verify property was written via HTTP
PVAL3=$(curl -sf "http://localhost:$PORT/prop?obj=301&prop=_test&sid=$SID1")
[ "$PVAL3" = "hello" ] && pass "sys_setprop verify" || fail "sys_setprop verify"

# create a non-admin player (used for the verb-owner authority checks below and
# in later sections that need an ordinary, non-wizard account)
curl -sf -X POST -d "$SID1 @invite new" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'New code:' || true
INVITE2=$(grep -o 'New code: [A-Z0-9-]*' /tmp/smolmoo_p1.log | tail -1 | sed 's/New code: //')
[ -n "$INVITE2" ] && pass "non-admin invite" || fail "non-admin invite"

curl -sN http://localhost:$PORT/events > /tmp/smolmoo_p3.log &
P3=$!
waitgrep /tmp/smolmoo_p3.log "^data: I" || true
SID3=$(grep -m1 "^data: I" /tmp/smolmoo_p3.log | sed 's/^data: I//')
[ -n "$SID3" ] && pass "session 3 ($SID3)" || fail "session 3"

curl -sf -X POST -d "$SID3 create TestPlayer3 pass3 $INVITE2" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'data: +' "create acct 3"

# --- OLC P2: privilege bracketing (seteuid model, see OLC.md) ---
# A verb runs with the CALLER's authority by default. testsetprop is not setuid,
# so a non-admin running it on an object it does not own is refused, even though
# it is installed system code.
curl -sf -X POST -d "$SID3 testsetprop Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'SETPROP:DENIED' "a non-setuid verb runs at caller authority (denied on a non-owned object)"

# testpriv IS setuid (owned by #0). It writes its dobj before elevating, after
# grant_accept, and after grant_release. For a non-admin on a non-owned object:
# caller authority is refused, elevated-to-#0 (wizard) succeeds, then refused
# again after release.
curl -sf -X POST -d "$SID3 testpriv Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'PRE:DENIED'  "before grant_accept a setuid verb still runs at caller authority"
check_log /tmp/smolmoo_p3.log 'POST:OK'     "grant_accept elevates a setuid verb to its owner (#0)"
check_log /tmp/smolmoo_p3.log 'DROP:DENIED' "grant_release drops back to caller authority"

# A player-programmed verb is not setuid, so grant_accept cannot elevate it: a
# non-admin cannot escalate by calling grant_accept in a verb they own.
curl -sf -X POST -d "$SID3 @create #400" http://localhost:$PORT/cmd >/dev/null
P3V=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p3.log | tail -1 | grep -o '[0-9]*')
printf '#include "mulibc.h"\nint main(void){grant_accept();int rc=sys_setprop(vm_args->dobj,"_p3","x");puts(rc==0?"P3V:OK":"P3V:DENIED");_exit(0);}\n' \
	| curl -sf -X POST --data-binary @- \
	  "http://localhost:$PORT/prop?obj=$P3V&prop=src&sid=$SID3" >/dev/null
curl -sf -X POST -d "p3set" \
	"http://localhost:$PORT/prop?obj=$P3V&prop=verb&sid=$SID3" >/dev/null
curl -sf -X POST -d "$SID3 @set #$P3V.args=<obj>" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @program #$P3V" http://localhost:$PORT/cmd >/dev/null
# grant_accept fails (no setuid), so the write to a non-owned object is refused.
curl -sf -X POST -d "$SID3 p3set Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'P3V:DENIED' "grant_accept cannot elevate a non-setuid player verb"
# The same verb can still write objects TestPlayer3 owns, at caller authority.
curl -sf -X POST -d "$SID3 @create #300" http://localhost:$PORT/cmd >/dev/null
P3OBJ=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p3.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID3 @set #$P3OBJ.name=widget3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @set #$P3OBJ.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 p3set widget3" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'P3V:OK' "a player verb writes its own object at caller authority"

# --- OLC-1: builder toolkit (see OLC.md) ---
# @clone copies an object's properties into a new object owned by the cloner.
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
OC=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$OC.name=gizmo" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @clone #$OC" http://localhost:$PORT/cmd >/dev/null
CLONE=$(grep -oE 'as #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
NM=$(curl -sf "http://localhost:$PORT/prop?obj=$CLONE&prop=name&sid=$SID1")
[ "$NM" = "gizmo" ] && pass "@clone copies properties" || fail "@clone copies properties"

# @move relocates an object you own; @find and @contents locate it.
curl -sf -X POST -d "$SID1 @move #$OC to #101" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "Moved #$OC to #101" "@move relocates an owned object"
curl -sf -X POST -d "$SID1 @find gizmo" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'gizmo' "@find matches by name"
curl -sf -X POST -d "$SID1 @contents #101" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "#$OC" "@contents lists a room's objects"

# @recycle is gated by ownership: a non-owner is refused, the owner succeeds.
curl -sf -X POST -d "$SID3 @recycle #$OC" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log "don't own that" "@recycle is refused to a non-owner"
curl -sf -X POST -d "$SID1 @recycle #$OC" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "Recycled #$OC" "@recycle destroys an owned object"
curl -sf -X POST -d "$SID1 @examine #$OC" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Object not found' "the recycled object is gone"

# @dig creates a room and a linked exit pair (admin owns the lobby as a wizard).
curl -sf -X POST -d "$SID1 @dig hatch to Workshop" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Dug #' "@dig creates a room and exit"
DUGEXIT=$(grep -oE 'Dug #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
DNM=$(curl -sf "http://localhost:$PORT/prop?obj=$DUGEXIT&prop=name&sid=$SID1")
[ "$DNM" = "hatch" ] && pass "@dig names the forward exit" || fail "@dig names the forward exit"
# A non-owner cannot dig out of a room they do not own.
curl -sf -X POST -d "$SID3 @dig sneak to Nowhere" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log "don't own this room" "@dig is refused to a non-owner of the room"

# @go teleports the builder and is wizard-only.
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You go to #101' "@go teleports the builder"
curl -sf -X POST -d "$SID3 @go #101" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'not authorized' "@go is refused to a non-wizard"

# @recycle refuses to orphan: an object with children (a prototype) or contents.
curl -sf -X POST -d "$SID1 @recycle #300" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'children or contents' "@recycle refuses to orphan a prototype"

# @clone preserves per-property permission flags: a non-world-readable prop
# stays hidden on the copy instead of reverting to the default rw,r,r.
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
SEC=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SEC.stash:rw,r,=loot" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @clone #$SEC" http://localhost:$PORT/cmd >/dev/null
SC=$(grep -oE 'as #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+' | head -1)
# plain -s (not -f): a denied read returns 403, which -f would treat as an error.
OTH=$(curl -s "http://localhost:$PORT/prop?obj=$SC&prop=stash&sid=$SID3")
[ "$OTH" != "loot" ] && pass "@clone preserves a non-readable flag (non-owner denied)" \
	|| fail "@clone preserves a non-readable flag"

# --- OLC-2: reset rules repopulate a room (see OLC.md) ---
# A fresh room plus a reset rule that keeps two raiders (children of #201) in it.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
RM=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$RM.name=Pit" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #910" http://localhost:$PORT/cmd >/dev/null
RULE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$RULE.room=#$RM" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RULE.proto=#201" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RULE.count=2" http://localhost:$PORT/cmd >/dev/null
# first reconcile spawns the two missing raiders
curl -sf -X POST -d "$SID1 @reset" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '2 spawned' "@reset spawns missing instances"
# idempotent: a second pass adds nothing while the room is full
curl -sf -X POST -d "$SID1 @reset" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '0 spawned' "@reset is idempotent while the room is full"
# down one instance; the next pass replaces exactly that one
curl -sf -X POST -d "$SID1 @contents #$RM" http://localhost:$PORT/cmd >/dev/null
CH=$(grep -oE '#[0-9]+  raider' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+' | head -1)
curl -sf -X POST -d "$SID1 @set #$CH.downed=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @reset" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '1 spawned' "@reset replaces a downed instance"

# clean spawn (OLC-2): a clone must not inherit its proto's combat state. Build a
# proto that has "been in combat" (dead, downed, hurt), clone it, and check the
# instance starts at full BP and clear of every death flag.
curl -sf -X POST -d "$SID1 @create #201" http://localhost:$PORT/cmd >/dev/null
CPROTO=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$CPROTO.name=gonk" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CPROTO.mig=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CPROTO.dead=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CPROTO.downed=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CPROTO.bp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CPROTO.wounds=3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
CRM=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @create #910" http://localhost:$PORT/cmd >/dev/null
CRULE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$CRULE.room=#$CRM" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CRULE.proto=#$CPROTO" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CRULE.count=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @reset" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #$CRM" http://localhost:$PORT/cmd >/dev/null
GONK=$(grep -oE "#[0-9]+  gonk" /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+' | head -1)
curl -sf -X POST -d "$SID1 @examine #$GONK" http://localhost:$PORT/cmd >/dev/null
# bp 21 = full for mig 9 (12 + 9); the proto's bp was 1, so this proves the reset
check_log /tmp/smolmoo_p1.log 'bp = "21"' \
	"a reset clone starts at full BP, not its proto's combat state"
check_log /tmp/smolmoo_p1.log 'wounds = "0"' "a reset clone starts unwounded"
# tidy up so this rule does not affect later reset-rule counts
curl -sf -X POST -d "$SID1 @recycle #$GONK" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @recycle #$CRULE" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @recycle #$CRM" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @recycle #$CPROTO" http://localhost:$PORT/cmd >/dev/null

# corpse decay (OLC-2): a fallen reset-spawned body is reaped after its lootable
# window, so downed bodies do not pile up. A short decay clock fires the sweep at
# once. The body is placed in the builder's current room (#101) so the reap
# announcement reaches this session; the builder is not moved.
curl -sf -X POST -d "$SID1 @create #201" http://localhost:$PORT/cmd >/dev/null
DBODY=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DBODY.name=stiff" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DBODY.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DBODY.reset_spawn=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DBODY.downed=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DBODY.decay_tick=1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'The stiff has been carried off' \
	"a fallen body is reaped after its lootable window"

# --- M19: sys_objfind ---

# resolve existing object by name
curl -sf -X POST -d "$SID1 testobjfind Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'OBJFIND:OK' "sys_objfind ok"

# resolve non-existent object
curl -sf -X POST -d "$SID1 testobjfind Nonexistent Thing" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'OBJFIND:NONE' "sys_objfind none"

# --- M21: sys_create / sys_recycle ---
curl -sf -X POST -d "$SID1 testcreate" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'CREATE:OK' "sys_create/recycle"

# --- M21: sys_call verb-to-verb dispatch ---
curl -sf -X POST -d "$SID1 testcall" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'CALL:OK' "sys_call resolves"
check_log /tmp/smolmoo_p1.log 'GREETED:world' "sys_call runs target verb"
check_log /tmp/smolmoo_p1.log 'HASVERB:YES' "sys_hasverb finds a verb"
check_log /tmp/smolmoo_p1.log 'HASVERB:MISS' "sys_hasverb rejects a missing verb"

# --- M21: richer verb-call argument marshalling (obj -> dobj, str -> argstr) ---
curl -sf -X POST -d "$SID1 testref" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'REFLECT:MATCH' "verb call delivers object arg as dobj"
check_log /tmp/smolmoo_p1.log 'REFLECT:NOMATCH' "verb call distinguishes object args"
check_log /tmp/smolmoo_p1.log 'GREETED:hi' "verb call delivers string arg as argstr"

# --- M21: @program compiles MooScript from a property into a live verb ---
curl -sf -X POST -d "$SID1 @create #400" http://localhost:$PORT/cmd >/dev/null
PROG=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
printf 'verb main(player: obj, room: obj)\n    player:tell("PROGRAMMED OK");\nendverb\n' \
	| curl -sf -X POST --data-binary @- \
	  "http://localhost:$PORT/prop?obj=$PROG&prop=src&sid=$SID1" >/dev/null
curl -sf -X POST -d "myprog" \
	"http://localhost:$PORT/prop?obj=$PROG&prop=verb&sid=$SID1" >/dev/null
curl -sf -X POST -d "$SID1 @program #$PROG" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Programmed.' "@program compiles source"
curl -sf -X POST -d "$SID1 myprog" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'PROGRAMMED OK' "@program verb runs"

# --- OLC P1: an @-prefixed command routes to a verb (see OLC.md) ---
# A verb whose `verb` property is "@olctest" is reached by typing @olctest,
# instead of the input being logged as feedback.
curl -sf -X POST -d "$SID1 @create #400" http://localhost:$PORT/cmd >/dev/null
OLCV=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
printf 'verb main(player: obj, room: obj)\n    player:tell("OLC ROUTED");\nendverb\n' \
	| curl -sf -X POST --data-binary @- \
	  "http://localhost:$PORT/prop?obj=$OLCV&prop=src&sid=$SID1" >/dev/null
curl -sf -X POST --data-raw "@olctest" \
	"http://localhost:$PORT/prop?obj=$OLCV&prop=verb&sid=$SID1" >/dev/null
curl -sf -X POST -d "$SID1 @program #$OLCV" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @olctest" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'OLC ROUTED' "@-command routes to a verb"
# An unmatched @tag with no verb still falls through to the feedback log.
curl -sf -X POST -d "$SID1 @gripe still works" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'your feedback has been noted' "unmatched @tag still logs feedback"

# @program also compiles a C verb; the language is sniffed from #include.
curl -sf -X POST -d "$SID1 @create #400" http://localhost:$PORT/cmd >/dev/null
CPROG=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
printf '#include "mulibc.h"\nint main(void) { puts("CPROG OK"); _exit(0); }\n' \
	| curl -sf -X POST --data-binary @- \
	  "http://localhost:$PORT/prop?obj=$CPROG&prop=src&sid=$SID1" >/dev/null
curl -sf -X POST -d "cprog" \
	"http://localhost:$PORT/prop?obj=$CPROG&prop=verb&sid=$SID1" >/dev/null
curl -sf -X POST -d "$SID1 @program #$CPROG" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 cprog" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'CPROG OK' "@program compiles and runs a C verb"

# --- M25a: sys_random hypercall ---
curl -sf -X POST -d "$SID1 testrandom" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'RANDOM:OK' "sys_random in range"

# --- M25b: sys_random and roll_pool distribution ---
curl -sf -X POST -d "$SID1 testdist" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'DIST:OK' "roll distribution sane"

# --- M25a: character sheet and skill check ---
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '=== TestPlayer1 ===' "sheet name"
check_log /tmp/smolmoo_p1.log 'Agility 2D' "sheet inherits stats"
check_log /tmp/smolmoo_p1.log 'Defense 6  Soak 4' "sheet derived stats"

curl -sf -X POST -d "$SID1 check firearms 6" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'CHECK firearms: pool 2D' "skill check rolls"

# --- M25a: interactive character creation (mutates TestPlayer1) ---
cg() { curl -sf -X POST -d "$SID1 chargen $1" http://localhost:$PORT/cmd >/dev/null; }
cg ""
check_log /tmp/smolmoo_p1.log 'Step 1/3 attributes' "chargen start"
cg "3 3 3 3"
check_log /tmp/smolmoo_p1.log 'must total 24 (you have 12)' "chargen attr sum"
cg "6 9 6 3"
check_log /tmp/smolmoo_p1.log 'Attributes set.' "chargen attrs accepted"
cg "bogus"
check_log /tmp/smolmoo_p1.log 'Hook must be street' "chargen bad hook"
cg "cyber"
check_log /tmp/smolmoo_p1.log 'Hook set.' "chargen hook accepted"
cg "spellcasting:6 hacking:6"
check_log /tmp/smolmoo_p1.log 'need the Awakened hook' "chargen spellcasting gate"
cg "firearms:6 hacking:3"
check_log /tmp/smolmoo_p1.log 'must total 12 (you have 9)' "chargen skill sum"
cg "firearms:6 command:6"
check_log /tmp/smolmoo_p1.log 'Character complete' "chargen complete"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Might 3D' "chargen wrote attributes"
check_log /tmp/smolmoo_p1.log 'Defense 10  Soak 6' "chargen recalc with hook"
check_log /tmp/smolmoo_p1.log 'BP 21/21' "chargen set BP to max"
check_log /tmp/smolmoo_p1.log 'Grit 6/6' "chargen set Grit to max"
check_log /tmp/smolmoo_p1.log 'firearms 2D' "chargen wrote skills"

# --- M25d: Grit on the fuel gauge ---
# The status bar carries the live Grit gauge next to Fuel. Max Grit is
# 3 + Wit dice + Charm dice: the 6/9/6/3 build gives 3 + 2 + 1 = 6, and
# chargen left current Grit at that max. The value is read from the
# creature's grit property (its store of record), which the status refreshes.
check_log /tmp/smolmoo_p1.log 'Grit: 6/6' "status bar shows the Grit gauge"

# --- M25d-4: maneuvers (the smartlink CP unlock) ---
# Chargen seeds the smartlink maneuver, so the sheet lists it. A maneuver is a
# combat action: using it outside a fight is refused. The in-fight effect (aim
# then a target-lowering attack) is exercised in the enforcer fight below.
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Maneuvers: smartlink' "sheet lists learned maneuvers"
curl -sf -X POST -d "$SID1 use smartlink" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'not in a fight' "a maneuver needs a fight"

# --- M25c: combat (TestPlayer1 has a firearms build from chargen above) ---
curl -sf -X POST -d "$SID1 wield pistol" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'ready the pistol' "wield weapon"
curl -sf -X POST -d "$SID1 attack raider" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'combat begins' "combat starts"
check_log /tmp/smolmoo_p1.log 'your turn' "turn prompt"
check_log /tmp/smolmoo_p1.log 'HP \[' "turn prompt renders the HP/Grit meters"
check_log /tmp/smolmoo_p1.log 'to hit)' "foe difficulty scale"
# Resolve the player's turn; retry across rounds until the mook Downs.
_i=0
while [ $_i -lt 8 ]; do
	curl -sf -X POST -d "$SID1 attack raider" http://localhost:$PORT/cmd >/dev/null
	waitgrep /tmp/smolmoo_p1.log 'fight is over' && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'It drops' "mook downed on first hit"
check_log /tmp/smolmoo_p1.log 'fight is over' "combat ends"
# M35a: winning the fight awards Character Points, one per defeated foe with no
# `cp_award` of its own. The raider is a single default mook, so the win is 1 CP.
check_log /tmp/smolmoo_p1.log 'You gain 1 Character Point' \
	"a won fight awards CP for the defeated foe"
# The downed raider cannot be attacked again.
curl -sf -X POST -d "$SID1 attack raider" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'already down' "downed foe rejected"

# --- M25c-2: cover and NPC grades (the enforcer is a tough behind cover) ---
# The Cover support action refuses outside a fight.
curl -sf -X POST -d "$SID1 cover enforcer" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'not in a fight' "cover needs a fight"
# Positional cover (2) lifts the enforcer's Passive Defense 6 to 8, so its
# difficulty word reads "fair" rather than the raider's "trivial".
curl -sf -X POST -d "$SID1 attack enforcer" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'on the enforcer' "enforcer fight starts"
check_log /tmp/smolmoo_p1.log 'enforcer at engaged range (unhurt, fair to hit)' "cover raises foe difficulty"
# Take cover on our turn; the next prompt reflects the positional tier.
curl -sf -X POST -d "$SID1 takecover" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'ducks into cover' "takecover action"
check_log /tmp/smolmoo_p1.log 'in light cover' "positional cover shown"
# Smartlink (M25d-4): spend the action to aim, then the next attack ignores the
# enforcer's cover and drops its Passive Defense by 2, tagged [aimed]. Retry
# off-turn attempts are refused harmlessly until it is our turn.
_i=0
while [ $_i -lt 24 ]; do
	curl -sf -X POST -d "$SID1 use smartlink" http://localhost:$PORT/cmd >/dev/null
	grep -q 'takes aim' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'takes aim' "smartlink spends the action to aim"
_i=0
while [ $_i -lt 24 ]; do
	curl -sf -X POST -d "$SID1 attack enforcer" http://localhost:$PORT/cmd >/dev/null
	grep -q '\[aimed\]' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log '\[aimed\]' "the aimed attack fires with the smartlink bonus"
# A tough runs the full BP path: several hits to Down (never a mook's one-shot
# drop). "Downed!" is unique to the BP path, so it also proves the grade split.
_i=0
while [ $_i -lt 20 ]; do
	curl -sf -X POST -d "$SID1 attack enforcer" http://localhost:$PORT/cmd >/dev/null
	grep -q 'Downed!' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.4
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'Downed!' "tough downed via BP path"
# Confirm the enforcer fight has fully torn down (cb_active cleared) before the
# next fight opens; downing a foe alone does not, so an unconfirmed handoff let
# the next attack draw its foe into the dying scene instead of starting fresh.
fight_over enforcer || true

# --- M25c-3: range bands and movement (the gunner hangs back at range) ---
# Retry until the previous fight has fully torn down (cb_active clears a poll
# after the Downed line) and this fight actually starts.
_i=0
while [ $_i -lt 20 ]; do
	curl -sf -X POST -d "$SID1 attack gunner" http://localhost:$PORT/cmd >/dev/null
	grep -q 'on the gunner' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'on the gunner' "gunner fight starts"
# Fights open Engaged; leave melee, then open to Long over two turns.
waitgrep /tmp/smolmoo_p1.log 'gunner at ' || true
curl -sf -X POST -d "$SID1 disengage" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'gunner at short range' "disengage opens to short"
curl -sf -X POST -d "$SID1 retreat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'gunner at long range' "retreat opens to long"
# A pistol (short reach) firing at Long is a long shot; it still Downs the mook.
curl -sf -X POST -d "$SID1 attack gunner" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'at long range' "long shot penalty applied"
check_log /tmp/smolmoo_p1.log 'It drops' "gunner downed at range"
# Confirm teardown before the brute fight opens (see the enforcer note above).
fight_over gunner || true

# --- M25c-4: reactions (free Strike and guard), vs a tough brute ---
_i=0
while [ $_i -lt 20 ]; do
	curl -sf -X POST -d "$SID1 attack brute" http://localhost:$PORT/cmd >/dev/null
	grep -q 'on the brute' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'on the brute' "brute fight starts"
# Leaving melee with a normal Move draws the foe's free Strike.
waitgrep /tmp/smolmoo_p1.log 'brute at ' || true
curl -sf -X POST -d "$SID1 retreat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Free strike' "retreat draws a free strike"
# Ready guard: the next attack on us is braced (+2 PD, tagged "guarded").
_i=0
while [ $_i -lt 15 ]; do
	curl -sf -X POST -d "$SID1 guard" http://localhost:$PORT/cmd >/dev/null
	curl -sf -X POST -d "$SID1 attack brute" http://localhost:$PORT/cmd >/dev/null
	grep -q 'guarded' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.4
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'set yourself to guard' "guard readied free"
check_log /tmp/smolmoo_p1.log 'guarded' "guard braces the incoming hit"
# Finish the brute so the fight tears down before later tests.
fight_over brute || true

# --- M25c-5: multi-foe fights and frontage (three gangers) ---
_i=0
while [ $_i -lt 20 ]; do
	curl -sf -X POST -d "$SID1 attack sentry" http://localhost:$PORT/cmd >/dev/null
	grep -q 'on the sentry' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'on the sentry' "multi-foe fight starts"
# Draw a second and third ganger into the fight, one per turn.
waitgrep /tmp/smolmoo_p1.log 'sentry at engaged' || true
curl -sf -X POST -d "$SID1 attack picket" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'picket at engaged range' "second foe joins the melee"
curl -sf -X POST -d "$SID1 attack straggler" http://localhost:$PORT/cmd >/dev/null
# Frontage caps the melee at two, so the third foe is held at Short. It first
# shows in the next round's prompt, so allow for the intervening NPC turns.
_i=0
while [ $_i -lt 20 ]; do
	grep -q 'straggler at short range' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.4
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'straggler at short range' "frontage holds the third at short"
# Both engaged foes take their turns against the player.
check_log /tmp/smolmoo_p1.log 'sentry attacks' "first foe acts"
check_log /tmp/smolmoo_p1.log 'picket attacks' "second foe acts"
# Clear the fight before later tests.
fight_over sentry picket straggler || true

# --- M25c-6: surprise round (ambush from stealth) ---
# Give the player a Stealth build so the approach beats the target's Passive
# Perception every time. Stealth 6 + Agility 6 rolls 4D (min 4); the lookout
# has Wit 0, so its Passive Perception is 4, and 4D never falls short.
cg ""
cg "6 9 6 3"
cg "cyber"
cg "stealth:6 firearms:6"
check_log /tmp/smolmoo_p1.log 'Character complete' "stealth build for ambush"
curl -sf -X POST -d "$SID1 wield pistol" http://localhost:$PORT/cmd >/dev/null
# Open from hiding: the strike lands before initiative, target Exposed. The
# ambush refuses while the previous multi-foe fight is still tearing down, so
# retry, draining any leftover gangers, until it actually fires.
_i=0
while [ $_i -lt 30 ]; do
	curl -sf -X POST -d "$SID1 ambush lookout" http://localhost:$PORT/cmd >/dev/null
	grep -q 'strike from hiding' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 attack sentry" http://localhost:$PORT/cmd >/dev/null
	curl -sf -X POST -d "$SID1 attack picket" http://localhost:$PORT/cmd >/dev/null
	curl -sf -X POST -d "$SID1 attack straggler" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'strike from hiding' "surprise strike lands"
# The ambushed foe loses its first round to being caught flat-footed. Advance
# our own turn with a harmless action (not another attack, which could drop the
# already-wounded foe before its skipped turn is reached).
_i=0
while [ $_i -lt 12 ]; do
	waitgrep /tmp/smolmoo_p1.log 'caught flat-footed' && break
	curl -sf -X POST -d "$SID1 takecover" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'caught flat-footed' "surprised foe loses the round"
# Stealth is impossible once the fight is joined.
curl -sf -X POST -d "$SID1 ambush lookout" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'No time for stealth' "ambush refused mid-fight"
# Down the lookout so the fight tears down before the next block; confirm the
# teardown so the flee scenario below opens a clean fight rather than drawing
# the thug into a lingering one.
fight_over lookout || true

# --- M25c-7: cross-room movement and fleeing a fight ---
# Refresh to full BP, then start a fight to flee from. The retry loop also
# waits out the teardown of the previous (lookout) fight.
cg ""
cg "6 9 6 3"
cg "cyber"
cg "stealth:6 firearms:6"
curl -sf -X POST -d "$SID1 wield pistol" http://localhost:$PORT/cmd >/dev/null
# Start the fight, then POLL for it to open rather than re-attacking: a second
# attack would land damage and could drop the tough thug before we flee, which
# needs it alive. (No stale 'on the thug' precedes this first thug fight.)
_i=0
while [ $_i -lt 10 ]; do
	curl -sf -X POST -d "$SID1 attack thug" http://localhost:$PORT/cmd >/dev/null
	waitgrep /tmp/smolmoo_p1.log 'on the thug' && break
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'on the thug' "flee fight starts"
# settle for the first prompt; the flee loop below retries regardless
waitgrep /tmp/smolmoo_p1.log 'thug at ' || true
# Flee through the exit on our turn: it draws the engaged foe's parting strike
# and ends our part in the fight, leaving us in the adjacent room.
_i=0
while [ $_i -lt 15 ]; do
	curl -sf -X POST -d "$SID1 flee alley" http://localhost:$PORT/cmd >/dev/null
	grep -q 'slip away from the fight' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.4
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'breaks for the alley' "flee announced"
check_log /tmp/smolmoo_p1.log 'Free strike' "flight draws a parting strike"
check_log /tmp/smolmoo_p1.log 'slip away from the fight' "flee leaves the room"
# The break-off is broadcast to the room the player left, so the bystander in
# the lobby (player 2) sees it, not the fled player.
check_log /tmp/smolmoo_p2.log 'fight breaks off' "combat ends when the player flees"
# Host really relocated us: the status line names the Alley, and the thug we
# left in the lobby is now out of scope.
check_log /tmp/smolmoo_p1.log 'The Alley' "status line follows the move"
curl -sf -X POST -d "$SID1 attack thug" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "don't see that here" "foe unreachable from another room"
# Plain travel back through the return exit.
curl -sf -X POST -d "$SID1 go lobby" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'heads out through the lobby' "go leaves the room"
check_log /tmp/smolmoo_p1.log '=== The Lobby ===' "go shows the destination"

# --- M25h: room span (a room bounds how far a fight can spread) ---
# Placed here while the fighter is still unburdened: it can travel between rooms
# and its retreat is not Slowed, so the span-0 refuse reads cleanly. The grand
# Lobby is span 2 (the gunner above retreated to Long there); the Vault is
# span 0, a box with no ground to give.
curl -sf -X POST -d "$SID1 go vault" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '=== The Vault ===' "enter the span-0 vault"
_i=0
while [ $_i -lt 10 ]; do
	curl -sf -X POST -d "$SID1 attack drone" http://localhost:$PORT/cmd >/dev/null
	waitgrep /tmp/smolmoo_p1.log 'on the drone' && break
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'on the drone' "vault fight starts"
social_drive "retreat" 'no room to fall back' || true
check_log /tmp/smolmoo_p1.log 'no room to fall back here' "span 0 refuses a retreat"
# Break off (still our turn after the refused move) and return to the lobby.
social_drive "go out" '=== The Lobby ===' || true
check_log /tmp/smolmoo_p1.log 'The Lobby' "return from the vault"

# --- M25c-8: command-set finish (hold, remove, prompt actions) ---
# hold is a combat action; it refuses outside a fight.
curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'not in a fight' "hold needs a fight"
# remove frees a gear slot: unwield a weapon, and unwear armor (soak drops).
curl -sf -X POST -d "$SID1 wield pistol" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 remove pistol" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'put away the pistol' "remove unwields a weapon"
curl -sf -X POST -d "$SID1 wear vest" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Soak 8' "worn armor raises soak"
curl -sf -X POST -d "$SID1 remove vest" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'take off the vest' "remove unwears armor"
# hold in a fight passes the turn cleanly, and the prompt lists the actions.
curl -sf -X POST -d "$SID1 wield pistol" http://localhost:$PORT/cmd >/dev/null
_i=0
while [ $_i -lt 20 ]; do
	curl -sf -X POST -d "$SID1 attack thug" http://localhost:$PORT/cmd >/dev/null
	grep -q 'on the thug' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'on the thug' "hold fight starts"
check_log /tmp/smolmoo_p1.log 'Actions: attack, cover' "turn prompt lists actions"
_i=0
while [ $_i -lt 15 ]; do
	curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
	grep -q 'holds, watching' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.4
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'holds, watching' "hold passes the turn"
# Down the thug so the fight tears down before the CLI tests.
fight_over thug || true

# --- M25d-2: push a roll (spend Grit for +ND) ---
# Run after combat so the gauge stays at max through the fights (no downtime
# regen ticking mid-combat). Pushing a check spends Grit for extra dice: the
# firearms pool is 4D, push 1 makes it 5D, spends 1 Grit, and drops the gauge.
curl -sf -X POST -d "$SID1 check firearms 6 push 1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'pool 5D' "push adds a die to the pool"
check_log /tmp/smolmoo_p1.log 'pushed +1D' "push tags the roll"
check_log /tmp/smolmoo_p1.log 'Grit: 5/6' "push spent one Grit off the gauge"

# --- M25d-3: stims and the money sink ---
# A stim costs an action, restores 2 Grit, and burns a dose off the kit.
# The push above left Grit at 5/6, so a stim tops it back up. Buying a stim
# draws 75 creds off the sheet: the primary Grit money sink.
curl -sf -X POST -d "$SID1 use stim" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Grit surges back' "use stim restores Grit"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Stims 2' "using a stim spends a dose"
curl -sf -X POST -d "$SID1 buy stim" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Balance 225' "buying a stim draws down creds"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Creds 225' "the spend persists on the sheet"

# --- M25e: social conflict (Section 16) ---
# Talk runs on the combat engine with cb_mode="social", so a social scene is
# driven the way fight_over drives a fight: social_drive sends a command, then
# polls for the outcome before sending the next, so the command that ends the
# scene is never followed by a stray one that reopens it. Each scene's yield
# clears cb_active synchronously, so the next scene opens fresh with no bleed.

# The informant is a soft target: open, prove a fight action is refused mid-
# argument (the shared cb_* state makes the two modes exclusive), then push to
# the default yield and teardown.
social_drive "command informant" 'square off with the informant' || true
check_log /tmp/smolmoo_p1.log 'square off with the informant' "social scene opens"
check_log /tmp/smolmoo_p1.log 'RESOLVE \[' "social prompt renders the Resolve meter"
curl -sf -X POST -d "$SID1 attack informant" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'middle of an argument' "a fight action is refused mid-argument"
social_drive "command informant" 'matter is settled' || true
check_log /tmp/smolmoo_p1.log 'informant (command)' "the push rolls Charm plus the approach skill"
check_log /tmp/smolmoo_p1.log 'They yield' "the target yields at 0 Resolve"
check_log /tmp/smolmoo_p1.log 'matter is settled' "the social scene tears down on a yield"

# The broker scripts an on_yield hook. Drive it to concede (its scene settles),
# then wait for the hook: it is a spawned task that slides a datachip across a
# beat later, proving the hook runs and the yielded NPC id is delivered. No
# command is sent while waiting, so the settled scene is not reopened.
social_drive "negotiate broker" 'matter is settled' || true
_i=0 _hook=0
while [ $_i -lt 100 ]; do
	grep -q 'slides the datachip' /tmp/smolmoo_p1.log 2>/dev/null && { _hook=1; break; }
	sleep 0.1
	_i=$((_i + 1))
done
[ "$_hook" = 1 ] && pass "the on_yield hook fires on an NPC concession" \
	|| fail "the on_yield hook fires on an NPC concession"

# The menace is a strong pusher. Open the scene, then hold while it grinds the
# player's Resolve down; at 0 the player backs down and the scene ends.
social_drive "command menace" 'square off with the menace' || true
check_log /tmp/smolmoo_p1.log 'square off with the menace' "the menace scene opens"
social_drive "hold" 'back down' || true
check_log /tmp/smolmoo_p1.log 'menace leans on' "the NPC pushes back on its turn"
check_log /tmp/smolmoo_p1.log 'back down' "the player yields at 0 Resolve"

# --- M25f: factions and standing (Section 14) ---
# The quartermaster in the lobby deals for the Combine. Standing with that
# faction shifts stim prices 10 percent per step and gates the sale entirely at
# Hostile or worse. Standing is GM-adjudicated, set with the admin-only
# @standing command; TestPlayer1 is the bootstrap admin, TestPlayer3 is not.

# Friendly (+2) sells at 80 percent: 75 -> 60.
curl -sf -X POST -d "$SID1 @standing TestPlayer1 combine 2" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'now stands Friendly (2) with combine' "@standing sets a faction step"
curl -sf -X POST -d "$SID1 buy stim" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'buy a stim for 60 creds' "good standing discounts the price"
# Watched (-1) sells at 110 percent: 75 -> 82.
curl -sf -X POST -d "$SID1 @standing TestPlayer1 combine -1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 buy stim" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'buy a stim for 82 creds' "poor standing raises the price"
# Hostile (-2): no deal.
curl -sf -X POST -d "$SID1 @standing TestPlayer1 combine -2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 buy stim" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'refuses to deal with you (Hostile)' "a Hostile faction refuses service"
# Standing shows on the sheet, and setting a second faction preserves the first.
curl -sf -X POST -d "$SID1 @standing TestPlayer1 ncpd 1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'combine Hostile  ncpd Known' "the sheet lists standing and @standing merges factions"
# @standing is admin-only; TestPlayer3 joined on a non-admin invite.
curl -sf -X POST -d "$SID3 @standing TestPlayer1 combine 3" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'not authorized' "@standing is refused to a non-admin"

# --- OLC-3: generic stores and vending machines (see OLC.md) ---
# A store is any object that holds priced item objects. A vending machine is
# the model with no vendor NPC: an object in the room whose contents are the
# stock. list shows it, buy moves one instance to the buyer and draws its
# price, and a reset rule with the machine as its room restocks it. TestPlayer1
# is in the lobby (#101) from the combat and social blocks above.
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
MACH=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$MACH.name=dispenser" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$MACH.location=#101" http://localhost:$PORT/cmd >/dev/null
# a cola prototype the reset rule clones into the machine as stock
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
COLA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$COLA.name=cola" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$COLA.price=5" http://localhost:$PORT/cmd >/dev/null
# a reset rule keeps two colas stocked in the machine
curl -sf -X POST -d "$SID1 @create #910" http://localhost:$PORT/cmd >/dev/null
SRULE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SRULE.room=#$MACH" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SRULE.proto=#$COLA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SRULE.count=2" http://localhost:$PORT/cmd >/dev/null
# the raider rule from OLC-2 is still full, so this pass stocks only the colas
curl -sf -X POST -d "$SID1 @reset" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '2 rule(s), 2 spawned' "@reset stocks a vending machine"
# list shows the store and its priced stock (no faction, so full price)
curl -sf -X POST -d "$SID1 list from dispenser" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'dispenser offers' "list names the store"
check_log /tmp/smolmoo_p1.log 'cola -- 5 creds' "list shows priced stock"
# buy moves one instance to the buyer and draws its price
curl -sf -X POST -d "$SID1 buy cola from dispenser" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You buy the cola for 5 creds' "buy sells a stock item"
# an item the store does not carry is refused
curl -sf -X POST -d "$SID1 buy widget from dispenser" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no such thing for sale' "buy refuses an item not in stock"
# the sold cola left the machine, so a reset pass restocks exactly one
curl -sf -X POST -d "$SID1 @reset" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '2 rule(s), 1 spawned' "@reset restocks a sold item"
# a non-admin can shop: buy is not setuid and writes only the buyer's own
# (self-owned) sheet, so it runs at caller authority (admin masks perms, so
# this must be proven as TestPlayer3). Fund the non-admin sheet first: read its
# charid off the player object, then set money on the sheet as admin.
curl -sf -X POST -d "$SID1 @contents #101" http://localhost:$PORT/cmd >/dev/null
P3E=$(grep -oE '&[0-9]+  TestPlayer3' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
curl -sf -X POST -d "$SID1 @examine &$P3E" http://localhost:$PORT/cmd >/dev/null
P3SH=$(grep -oE 'charid[^"]*"[0-9]+"' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+' | tail -1)
curl -sf -X POST -d "$SID1 @set #$P3SH.money=100" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 buy cola from dispenser" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'You buy the cola for 5 creds. Balance 95' "a non-admin completes a purchase (own-sheet write, no wizard)"

# --- M35b: spend CP to raise a skill (train) ---
# Raising a skill costs CP equal to its current rating in dice and is capped at
# attribute + 6 skill points, like creation. Combat is done by here, so nudging
# TestPlayer1's firearms does not disturb the fights above. Read the sheet id off
# the player object (the buy test's pattern), set a known CP budget, then train.
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #101" http://localhost:$PORT/cmd >/dev/null
P1E=$(grep -oE '&[0-9]+  TestPlayer1' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
curl -sf -X POST -d "$SID1 @examine &$P1E" http://localhost:$PORT/cmd >/dev/null
P1SH=$(grep -oE 'charid[^"]*"[0-9]+"' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+' | tail -1)
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 train firearms" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You train firearms to' \
	"train raises a skill and spends CP"
# an unknown skill is refused
curl -sf -X POST -d "$SID1 train bogusskill" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Unknown skill' "train refuses an unknown skill"
# with no CP the raise is refused, not applied
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 train stealth" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'That costs' "train refuses a raise the character cannot afford"

# --- M35c: spend CP on a 5 CP unlock (learn) ---
# `learn` adds a catalog id to the maneuvers list after checking its hook, its
# graft-slot cap, and its CP cost. TestPlayer1 took the cyber hook at chargen, so
# it can take the dermal cyberware; dermal raises Soak by 2, a real effect read
# live by cs_recalc. First the hook gate: a non-cyber character cannot take it.
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=street" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 learn dermal" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'needs the cyber hook' "learn gates cyberware on the hook"
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=cyber" http://localhost:$PORT/cmd >/dev/null
# with the hook back but no CP, the unlock is refused
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 learn dermal" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'That costs 5 CP' "learn refuses an unlock the character cannot afford"
# fund it, capture the baseline Soak, then learn dermal and confirm +2 Soak
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=20" http://localhost:$PORT/cmd >/dev/null
# M36a: with no id, learn lists the catalog and each entry's status. Cyber and
# funded, TestPlayer1 can take dermal, so it shows available.
curl -sf -X POST -d "$SID1 learn" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'dermal.*available' \
	"learn with no id lists the catalog with an available unlock"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'Maneuvers: smartlink' || true
SB=$(grep -oE 'Soak [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
curl -sf -X POST -d "$SID1 learn dermal" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You learn dermal' "learn acquires an unlock and spends CP"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'Maneuvers: smartlink,dermal' || true
SA=$(grep -oE 'Soak [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
[ "${SA:-0}" -eq "$(( ${SB:-0} + 2 ))" ] \
	&& pass "the learned dermal raises Soak by 2" \
	|| fail "the learned dermal raises Soak by 2"
# the catalog now marks the learned unlock owned
curl -sf -X POST -d "$SID1 learn" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'dermal.*owned' "the catalog marks a learned unlock owned"
# a known unlock and an unknown id are both refused
curl -sf -X POST -d "$SID1 learn dermal" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'already know dermal' "learn refuses an unlock already known"
curl -sf -X POST -d "$SID1 learn bogus" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'No such unlock' "learn refuses an unknown id"
# M36b: a passive unlock (dermal) is not something you activate with `use`
curl -sf -X POST -d "$SID1 use dermal" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'works on its own' "use refuses a passive unlock"

# --- M25g: body slots and the inventory flatten view (Section 9) ---
# TestPlayer1 has the standard human anatomy (no anatomy prop, so the default
# frame) and, from the combat block above, the pistol readied in a hand.
curl -sf -X POST -d "$SID1 inventory" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'slots:' "inventory prints the slot summary"
check_log /tmp/smolmoo_p1.log 'hand 1/2' "the readied pistol occupies one of two hands"
check_log /tmp/smolmoo_p1.log 'hand0 pistol' "the occupied slot lists its item"
# Role checks: a weapon cannot be worn, armor cannot be wielded.
curl -sf -X POST -d "$SID1 wear pistol" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'cannot wear that' "a weapon refuses a wear slot"
curl -sf -X POST -d "$SID1 wield vest" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'cannot wield that' "armor refuses a hold slot"
# A worn container: the satchel takes back0 (its part preference) and its
# contents show against capacity in the flatten view.
curl -sf -X POST -d "$SID1 wear satchel" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'put on the satchel' "wear a container onto its part"
curl -sf -X POST -d "$SID1 inv" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'back0 satchel (2/6): stim' "a container lists used/cap and contents"
# The second hand is free; wielding the knife fills it, a third weapon has no
# free hand.
curl -sf -X POST -d "$SID1 wield knife" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'ready the knife' "the free hand takes a second weapon"
curl -sf -X POST -d "$SID1 wield holdout" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no free hand' "both hands full refuses a third weapon"

# --- M25g slice 2: ammunition and encumbrance (rules Section 6) ---
# Free a hand and ready the empty test carbine (clip 2, seeded with 0 rounds).
curl -sf -X POST -d "$SID1 remove knife" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 wield carbine" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'ready the carbine' "ready the ammo weapon"
curl -sf -X POST -d "$SID1 inventory" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'carbine \[0/2\]' "inventory shows an empty magazine"
# Out of a fight, reloading is free and refills to the clip capacity.
curl -sf -X POST -d "$SID1 reload" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'reload the carbine' "reload refills out of combat"
curl -sf -X POST -d "$SID1 inventory" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'carbine \[2/2\]' "inventory shows the full magazine"

# By now TestPlayer1 carries a deterministic 8 load units: pistol 2, the vest
# left loose after M25c-8 (3), the knife just removed but still held (1), the
# worn satchel (1), and the carbine (1). Against a Might-3D carry rating of 6
# that gear alone is encumbering (Slowed in a fight), short of the overload cap.
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Load 8/6 (encumbered)' "carried gear encumbers a light frame"

# One fight against the training dummy exercises Slowed movement and the ammo
# economy in combat. The dummy is a tough that shrugs off the carbine, so the
# fight persists deterministically until we break off.
social_drive "attack dummy" 'move on the dummy' || true
check_log /tmp/smolmoo_p1.log 'move on the dummy' "dummy fight starts"
# Slowed: the first Move is a half step, the second completes the band.
# (close takes no argument; it advances on the nearest foe.)
social_drive "close" 'half a step' || true
check_log /tmp/smolmoo_p1.log 'strains under the load, half a step' "Slowed halves a Move"
social_drive "close" 'closes into melee' || true
check_log /tmp/smolmoo_p1.log 'closes into melee' "the second Move completes the band"
# Firing draws down the magazine; a resolved shot depletes a round.
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 attack dummy" http://localhost:$PORT/cmd >/dev/null
	curl -sf -X POST -d "$SID1 inventory" http://localhost:$PORT/cmd >/dev/null
	grep -q 'carbine \[1/2\]' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.1
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'carbine \[1/2\]' "firing in combat depletes a round"
# Empty the magazine, then the empty weapon just clicks and reloads in the fight.
social_drive "attack dummy" 'carbine is empty' || true
check_log /tmp/smolmoo_p1.log 'carbine is empty' "an empty weapon clicks in combat"
social_drive "reload" 'reload the carbine' || true
check_log /tmp/smolmoo_p1.log 'reload the carbine' "reload is an action in a fight"
# Break off (encumbered, not overloaded, so movement still works), then return.
social_drive "flee alley" 'slip away from the fight' || true
check_log /tmp/smolmoo_p1.log 'slip away from the fight' "an encumbered fighter can still flee"
social_drive "go lobby" '=== The Lobby ===' || true

# The heavy crate on top pushes the load past twice the carry rating: now
# overloaded, movement locks until weight is shed (8 + 7 = 15 > 12).
curl -sf -X POST -d "$SID1 wear crate" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Load 15/6 (overloaded)' "a heavy load overloads"
curl -sf -X POST -d "$SID1 go alley" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'too loaded down to move' "overload locks movement"

# --- M25g slice 3: get, drop, and put loose items (Section 9) ---
# The overloaded fighter is still in the lobby wearing the crate and the
# satchel. get/drop/put move only loose items; worn or wielded gear is refused
# and left to the remove verb.
curl -sf -X POST -d "$SID1 drop crate" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'remove it first' "drop refuses worn gear"
curl -sf -X POST -d "$SID1 put crate in satchel" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'remove it first' "put refuses worn gear"
# Reach one level into the worn satchel and pull an item out, then stow it back.
curl -sf -X POST -d "$SID1 get stim from satchel" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'take the stim from the satchel' "get pulls from an open container"
curl -sf -X POST -d "$SID1 put stim in satchel" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'stow the stim' "put stows a loose item in a container"
# Take the crate off (loose now), lift the belt pouch, and stow the crate: the
# single-item pouch fills to capacity.
curl -sf -X POST -d "$SID1 remove crate" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 get pouch" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'pick up the pouch' "get lifts an item off the floor"
curl -sf -X POST -d "$SID1 put crate in pouch" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'stow the crate' "put stows a loose item in a container"
# A second item overflows the one-slot pouch (cap is an item count, Section 9).
curl -sf -X POST -d "$SID1 get stim from satchel" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 put stim in pouch" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "won't fit in the pouch" "put enforces the container item cap"
# Drop the loose stim to the floor, then pick it back up.
curl -sf -X POST -d "$SID1 drop stim" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'drop the stim' "drop puts a loose item on the floor"

# --- M25j: cached load rollup follows moves through the subtree (sys_rollup) ---
# The fighter now carries 16 load units (pistol 2, vest 3, knife 1, satchel 1,
# carbine 1, and the pouch 1 holding the crate 7). Dropping the loose vest sheds
# just its 3 (a move invalidates the cached total, which recomputes to 13).
curl -sf -X POST -d "$SID1 drop vest" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Load 13/6' "a move invalidates and recomputes the cached load"
# Dropping the pouch takes the crate nested inside it too: 13 - (1 + 7) = 5, so
# the rollup follows the move recursively, not just the top-level item.
curl -sf -X POST -d "$SID1 drop pouch" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Load 5/6' "the rollup follows a container's whole subtree on a move"

# --- M25i: death and recovery (Section 11) ---
# @kill is the GM entry to the death track (a Might-3D fighter almost never
# fails all three cling rolls, so combat cannot reliably kill for a test). It is
# admin-only; TestPlayer3 joined on a non-admin invite.
curl -sf -X POST -d "$SID3 @kill TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'not authorized' "@kill is refused to a non-admin"
curl -sf -X POST -d "$SID1 @kill TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'struck down' "@kill drops a player into the dead state"
# A dead body cannot act: action verbs are refused.
curl -sf -X POST -d "$SID1 attack dummy" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You are dead' "a dead player cannot act"
# Top up creds (the fighter spent most of its stake on stims) so the paid path
# is solvent and deterministic, then revive at once for the fee.
curl -sf -X POST -d "$SID1 @grant TestPlayer1 500" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 recover" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'clinic revives you for 100 creds' "paid recovery revives for a fee"
check_log /tmp/smolmoo_p1.log 'wake at the recovery point' "revival wakes at the recovery point"
# Now alive again: recover is rejected, which proves the revive cleared 'dead'.
curl -sf -X POST -d "$SID1 recover" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You are not dead' "recover is rejected once alive"
# Auto-recovery: die again and wait out the death timer (2s span in the test),
# which revives free at the recovery point. Poll for the second wake line.
curl -sf -X POST -d "$SID1 @kill TestPlayer1" http://localhost:$PORT/cmd >/dev/null
_i=0
while [ $_i -lt 60 ]; do
	_c=$(grep -c 'wake at the recovery point' /tmp/smolmoo_p1.log 2>/dev/null || echo 0)
	[ "${_c:-0}" -ge 2 ] && break
	sleep 0.3
	_i=$((_i + 1))
done
if [ "$(grep -c 'wake at the recovery point' /tmp/smolmoo_p1.log 2>/dev/null || echo 0)" -ge 2 ]; then
	pass "auto-recovery revives after the timer"
else
	fail "auto-recovery revives after the timer"
fi

# --- M25i ally slice: carry a body and revive it in place (Section 11) ---
# A fallen friend can be hauled to safety and patched by an ally with the
# training. TestPlayer2 is the medic, TestPlayer1 the casualty; @teach and @kill
# are GM tools (SID1 is the bootstrap admin). Both start in the lobby.
curl -sf -X POST -d "$SID1 @kill TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'struck down' "the casualty is down"
# A fallen body can be looted: an ally strips its worn gear one level deep,
# closing the M25i lootable-body deferral.
curl -sf -X POST -d "$SID2 get satchel from TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'strip the satchel from TestPlayer1' "get strips gear off a fallen body"
# Reviving needs Medicine or Cybertech; TestPlayer2 has neither yet.
curl -sf -X POST -d "$SID2 revive TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'lack the Medicine or Cybertech' "revive needs the training"
curl -sf -X POST -d "$SID1 @teach TestPlayer2 medicine 6" http://localhost:$PORT/cmd >/dev/null
# Carry the body, then set it down again.
curl -sf -X POST -d "$SID2 carry TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'take up the body' "carry lifts a body"
curl -sf -X POST -d "$SID2 release" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'set the body down' "release drops the body"
# Carry again, haul it through the exit to the alley, and revive it in place.
curl -sf -X POST -d "$SID2 carry TestPlayer1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID2 go alley" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'hauls a body along' "the body is dragged room to room"
# The revive succeeds only if the body actually travelled to the alley with us.
curl -sf -X POST -d "$SID2 revive TestPlayer1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p2.log 'patches TestPlayer1 back to life' "an ally revives the body in place"
check_log /tmp/smolmoo_p1.log 'back from the brink' "the casualty is alive again"

# --- OLC-4: reactive mob behavior (see OLC.md) ---
# Walking into a room runs each resident NPC's on_enter verb, driven by the
# mob's `behavior` prop: greet says a line, aggro opens a fight. Dig an ambush
# room off the lobby, stock it with a greeter and an aggressor, and walk in.
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @dig ambush to Ambush Nook" http://localhost:$PORT/cmd >/dev/null
AMB=$(grep -oE 'Dug #[0-9]+ to #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '#[0-9]+' | tail -1 | tr -d '#')
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
WARDEN=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$WARDEN.name=warden" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$WARDEN.behavior=greet" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$WARDEN.greeting=The warden looks you over." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$WARDEN.location=#$AMB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
BRUTE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BRUTE.name=brute" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRUTE.behavior=aggro" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRUTE.location=#$AMB" http://localhost:$PORT/cmd >/dev/null
# walk through the exit; the host wakes both mobs in the destination
curl -sf -X POST -d "$SID1 go ambush" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'The warden looks you over' "greet fires on room entry"
check_log /tmp/smolmoo_p1.log 'brute turns on' "aggro opens a fight on room entry"
# end the ambush fight cleanly: flee on our turn so __combat runs its teardown,
# with a cb_active reset as a fallback if a turn never comes up in time
_i=0
while [ $_i -lt 15 ]; do
	curl -sf -X POST -d "$SID1 flee back" http://localhost:$PORT/cmd >/dev/null
	grep -q 'slip away from the fight' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.4
	_i=$((_i + 1))
done
curl -sf -X POST -d "$SID1 @set #$AMB.cb_active=0" http://localhost:$PORT/cmd >/dev/null
# the entry hook also covers the @go teleport path, not just walking an exit
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
VLT=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$VLT.name=Sentinel Post" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
SENT=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SENT.name=sentinel" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENT.behavior=greet" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENT.greeting=The sentinel challenges you." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENT.location=#$VLT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$VLT" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'The sentinel challenges you' "@go teleport also triggers the entry hook"

# --- OLC P2 migration check: a non-admin runs the setuid combat verbs ---
# Placed after all admin combat so it cannot disturb those fights. Test accounts
# are admin, which masks permission checks, so prove the real path: the non-admin
# TestPlayer3 (still in the lobby) fights a fresh NPC. attack and the __combat
# turn task write the room and NPC (owner #0) and succeed only because those
# verbs are setuid and call grant_accept(); a missed grant would fail silently
# for a player.
# Clear any residual fight in the lobby left by the admin combats above, so
# TestPlayer3's attack opens a fresh fight rather than joining a stale one.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #201" http://localhost:$PORT/cmd >/dev/null
GOON=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$GOON.name=goon" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GOON.location=#101" http://localhost:$PORT/cmd >/dev/null
# The raider prototype #201 was downed in the combat above; the child inherits
# that, so clear it to field a fresh, standing foe.
curl -sf -X POST -d "$SID1 @set #$GOON.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 attack goon" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'combat begins' "non-admin attack starts a fight (setuid attack writes the room)"
check_log /tmp/smolmoo_p3.log 'your turn' "non-admin turn task renders (setuid __combat elevates)"
# The turn task does not intercept commands: an unknown command typed during a
# fight still reports as unknown, rather than being swallowed by the engine.
curl -sf -X POST -d "$SID3 floooble" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'Unknown command' \
	"combat does not hijack commands (unknown stays unknown mid-fight)"

# --- OLC-5: event system and runtime message loop (see OLC.md) ---
# An agent is a verb that links -lverbmain (the agent_ prefix), giving it a
# generic event loop instead of a one-shot main(). @wake #N starts object #N as
# an agent under the system session, with #N.brain naming the agent verb. The
# demo agent (__ticker, #452) increments a `ticks` prop each dwell and announces
# a newcomer on EV_ENTER. Two things are proven: the loop runs with no player
# present (ticks climb between two reads that issue no agent command), and a
# room entry is delivered to the live agent's mailbox (EV_ENTER).
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
BROOM=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BROOM.name=Beacon Bay" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
BEACON=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BEACON.name=beacon" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BEACON.location=#$BROOM" http://localhost:$PORT/cmd >/dev/null
# behavior makes mob_enter consider it; brain names the agent verb for @wake.
curl -sf -X POST -d "$SID1 @set #$BEACON.behavior=watch" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BEACON.brain=#452" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$BEACON" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log "Woke #$BEACON" "@wake starts the agent"

# read ticks helper: examine the beacon, return its latest ticks value.
beacon_ticks() {
	curl -sf -X POST -d "$SID1 @examine #$BEACON" http://localhost:$PORT/cmd >/dev/null
	waitgrep /tmp/smolmoo_p1.log 'ticks = "[0-9]' || true
	grep -o 'ticks = "[0-9]*"' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*' || true
}
# no agent command runs between these two reads, so any increase is autonomous.
T1=$(beacon_ticks)
sleep 0.8
T2=$(beacon_ticks)
[ "${T2:-0}" -gt "${T1:-0}" ] \
	&& pass "agent ticks climb with no player (EV_TIMER autonomy)" \
	|| fail "agent ticks climb with no player (EV_TIMER autonomy)"

# walk into the beacon's room: mob_enter routes an EV_ENTER to the live agent.
curl -sf -X POST -d "$SID1 @go #$BROOM" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'The beacon registers a visitor' \
	"room entry is delivered to the agent (EV_ENTER)"

# @wake marks the object awake in the persistent world, so a boot scan (at
# startup or after @rewind) re-wakes it with no wizard re-running @wake.
curl -sf -X POST -d "$SID1 @examine #$BEACON" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'awake = "1"' \
	"@wake marks the agent awake for boot-scan persistence"
# @sleep stops the agent and clears the flag: ticks stop and awake reads 0.
curl -sf -X POST -d "$SID1 @sleep #$BEACON" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'is now asleep' "@sleep stops a running agent"
TS1=$(beacon_ticks)
sleep 0.6
TS2=$(beacon_ticks)
[ "${TS2:-0}" -eq "${TS1:-0}" ] \
	&& pass "a slept agent stops ticking" \
	|| fail "a slept agent stops ticking"
curl -sf -X POST -d "$SID1 @examine #$BEACON" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'awake = "0"' "@sleep clears the awake flag"

# --- M33a: in-game agent authoring (@program #N agent, see M33.md) ---
# A builder writes an event-loop agent in the editor and compiles it with the
# `agent` token, which links -lverbmain like an agent_*.c source. Prove the
# compiled program runs as a real agent (its EV_TIMER fires), not a one-shot
# verb that would exit at once. SID1 is in BROOM here, so the agent's broadcast
# to its room lands in this session's log.
curl -sf -X POST -d "$SID1 @create #400" http://localhost:$PORT/cmd >/dev/null
WVERB=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
printf '#include "mulibc.h"\nint verb_dwell(void){return 200;}\nvoid on_event(const struct verb_event*m){if(m->type==EV_TIMER)sys_broadcast(vm_args->room,"the widget hums");}\n' \
	| curl -sf -X POST --data-binary @- \
	  "http://localhost:$PORT/prop?obj=$WVERB&prop=src&sid=$SID1" >/dev/null
curl -sf -X POST -d "__widget" \
	"http://localhost:$PORT/prop?obj=$WVERB&prop=verb&sid=$SID1" >/dev/null
curl -sf -X POST -d "$SID1 @program #$WVERB agent" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Programmed' "@program #N agent compiles an in-game agent"
# wake an object that names the freshly authored brain, in SID1's current room
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
WOBJ=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$WOBJ.location=#$BROOM" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$WOBJ.brain=#$WVERB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$WOBJ" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'the widget hums' \
	"an in-game authored agent runs its event loop (EV_TIMER)"
curl -sf -X POST -d "$SID1 @sleep #$WOBJ" http://localhost:$PORT/cmd >/dev/null

# --- M33b: ownership quota (see M33.md) ---
# Open building is capped per account so it cannot exhaust the id space.
# Wizards are exempt, so this is checked as the non-admin TestPlayer3, who
# already owns objects from the permission tests. #0.objquota tunes the cap
# live: set it below TestPlayer3's count and the next create is refused; raise
# it and creation resumes. `Created #` is not unique in the log, so the create
# assertions compare its count before and after (a delta), not mere presence.
curl -sf -X POST -d "$SID1 @set #0.objquota=1" http://localhost:$PORT/cmd >/dev/null
Q1=$(grep -c 'Created #' /tmp/smolmoo_p3.log 2>/dev/null || true)
curl -sf -X POST -d "$SID3 @create #300" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'object quota' \
	"a non-wizard over quota is refused a new object"
Q2=$(grep -c 'Created #' /tmp/smolmoo_p3.log 2>/dev/null || true)
[ "${Q2:-0}" -eq "${Q1:-0}" ] \
	&& pass "the refused create made no object" \
	|| fail "the refused create made no object"
# an admin is exempt even at the low quota (use Q-counters, not P1/P2, which
# hold the SSE listener PIDs the cleanup trap kills)
QW1=$(grep -c 'Created #' /tmp/smolmoo_p1.log 2>/dev/null || true)
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
_qi=0; while [ $_qi -lt 60 ]; do
	QW2=$(grep -c 'Created #' /tmp/smolmoo_p1.log 2>/dev/null || true)
	[ "${QW2:-0}" -gt "${QW1:-0}" ] && break
	sleep 0.05; _qi=$((_qi + 1))
done
[ "${QW2:-0}" -gt "${QW1:-0}" ] \
	&& pass "a wizard is exempt from the quota" \
	|| fail "a wizard is exempt from the quota"
# raise the quota; TestPlayer3 can build again
curl -sf -X POST -d "$SID1 @set #0.objquota=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @create #300" http://localhost:$PORT/cmd >/dev/null
_qi=0; while [ $_qi -lt 60 ]; do
	Q3=$(grep -c 'Created #' /tmp/smolmoo_p3.log 2>/dev/null || true)
	[ "${Q3:-0}" -gt "${Q2:-0}" ] && break
	sleep 0.05; _qi=$((_qi + 1))
done
[ "${Q3:-0}" -gt "${Q2:-0}" ] \
	&& pass "raising the quota lets a builder create again" \
	|| fail "raising the quota lets a builder create again"

# --- M33c: prototype discovery (see M33.md) ---
# @proto list shows the prototypes registered on #0 so a builder can find a
# parent for @create without reading source. Read-only and unprivileged, so
# check it as the non-admin TestPlayer3. The prototype names are unique to this
# listing in TestPlayer3's log, so presence is a sound assertion.
curl -sf -X POST -d "$SID3 @proto list" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'Room Prototype' \
	"@proto list shows the room prototype (non-admin)"
check_log /tmp/smolmoo_p3.log 'Reset Prototype' \
	"@proto list shows the reset prototype"

# --- OLC-6: proactive mob behavior (see OLC.md) ---
# A mob woken as an agent (brain #453, __rover) acts on its own: wander steps a
# random exit each tick, patrol follows a route, and it still greets on entry.
# Prove all three, plus that sys_getobj reads the live location (a wander mob
# that could not read its own room would get stuck or teleport wrongly).
# An isolated room pair keeps the moving mobs off the shared rooms above.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
RA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$RA.name=Rove West" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$RA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @dig east to Rove East" http://localhost:$PORT/cmd >/dev/null
RB=$(grep -oE 'Dug #[0-9]+ to #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '#[0-9]+' | tail -1 | tr -d '#')

# wander: the mob steps between the two rooms on its own, announcing each move.
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
DRIFT=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DRIFT.name=drifter" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DRIFT.location=#$RA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DRIFT.behavior=wander" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DRIFT.dwell=200" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DRIFT.brain=#453" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$DRIFT" http://localhost:$PORT/cmd >/dev/null
# no player command drives the mob; a wander broadcast proves the autonomous tick
check_log /tmp/smolmoo_p1.log 'drifter leaves' \
	"a woken mob wanders with no player (EV_TIMER moves it)"
# quiet it so it stops moving through the shared assertions that follow
curl -sf -X POST -d "$SID1 @set #$DRIFT.behavior=idle" http://localhost:$PORT/cmd >/dev/null

# patrol: a route-following mob announces its scheduled steps.
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
SENTRY=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SENTRY.name=sentry" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENTRY.location=#$RA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENTRY.behavior=patrol" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENTRY.route=$RA,$RB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENTRY.dwell=200" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SENTRY.brain=#453" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$SENTRY" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'sentry leaves' \
	"a woken mob patrols a route with no player (EV_TIMER)"
curl -sf -X POST -d "$SID1 @set #$SENTRY.behavior=idle" http://localhost:$PORT/cmd >/dev/null

# reactive still works for a woken mob: EV_ENTER routes to the agent, which
# greets. Give it a huge dwell so it does not wander during the check.
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
GUARD=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$GUARD.name=guard" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.location=#$RB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.behavior=greet" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.greeting=The guard salutes." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.dwell=999999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.brain=#453" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$GUARD" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$RB" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'The guard salutes' \
	"a woken mob still greets on entry (EV_ENTER via the agent)"

# --- OLC-7: vehicles (see OLC.md) ---
# A vehicle is a room object (vehicle=1) whose location is its current stop and
# whose riders have location = the vehicle, so moving it carries them. A timed
# train advances on EV_TIMER; an on-command elevator moves on a rider's command
# routed as EV_USER. Build a hub with two more stops, then a train and a lift.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
HUB=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$HUB.name=Depot" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @dig a to Level Two" http://localhost:$PORT/cmd >/dev/null
L2=$(grep -oE 'Dug #[0-9]+ to #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '#[0-9]+' | tail -1 | tr -d '#')
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @dig b to Level Three" http://localhost:$PORT/cmd >/dev/null
L3=$(grep -oE 'Dug #[0-9]+ to #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '#[0-9]+' | tail -1 | tr -d '#')
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null

# timed train: board, ride, and disembark. Its arrivals reach the rider inside.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
TRAIN=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$TRAIN.name=carriage" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TRAIN.description=A wooden carriage." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TRAIN.vehicle=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TRAIN.location=#$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TRAIN.route=$HUB,$L2,$L3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TRAIN.dwell=250" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TRAIN.brain=#456" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$TRAIN" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 board carriage" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'climbs aboard' "board puts the player aboard the vehicle"
# a self-paced train runs its own route and refuses a rider's floor request,
# so a rider cannot steer it off schedule
curl -sf -X POST -d "$SID1 floor 2" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'carriage runs a fixed route' \
	"a self-paced train refuses rider floor requests"
# the train moves on its own; a rider inside sees the scheduled arrival
check_log /tmp/smolmoo_p1.log 'carriage arrives at' \
	"a woken vehicle advances on its own and carries its rider (EV_TIMER)"
curl -sf -X POST -d "$SID1 disembark" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'steps off the carriage' "disembark drops the rider at the current stop"
# quiet the train so its later hops do not broadcast into the shared assertions
curl -sf -X POST -d "$SID1 @set #$TRAIN.route=" http://localhost:$PORT/cmd >/dev/null

# on-command elevator: no dwell, so it only moves on an EV_USER floor request.
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
LIFT=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$LIFT.name=elevator" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$LIFT.description=A cramped lift." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$LIFT.vehicle=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$LIFT.location=#$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$LIFT.route=$HUB,$L2,$L3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$LIFT.brain=#456" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$LIFT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 board elevator" http://localhost:$PORT/cmd >/dev/null
# the `floor` verb routes the request to the vehicle agent (sys_notify -> EV_USER)
curl -sf -X POST -d "$SID1 floor 3" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'elevator arrives at Level Three' \
	"the floor verb routes a stop request to the vehicle agent and moves it"
# the elevator refuses a floor outside its route rather than silently ignoring it
curl -sf -X POST -d "$SID1 floor 9" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'elevator has no such floor' \
	"an elevator refuses an out-of-range floor"
# with the catch-all gone, an unknown command aboard is just unknown, not
# swallowed by whatever agent runs the room
curl -sf -X POST -d "$SID1 wibble" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Unknown command' \
	"an unknown command aboard a vehicle is reported, not routed to the agent"
# disembarking the lift lands the rider at the floor it stopped on
curl -sf -X POST -d "$SID1 disembark" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'steps off the elevator' \
	"disembark leaves the lift at the requested floor"

# call: summon the line a platform names in its `line` prop, from the platform.
# Mark each stop as served by the lift; the lift is at Level Three now, so a
# call from the hub brings it there.
curl -sf -X POST -d "$SID1 @set #$HUB.line=#$LIFT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$L2.line=#$LIFT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$L3.line=#$LIFT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 call" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You signal the elevator' "call signals the platform's line"
check_log /tmp/smolmoo_p1.log 'elevator pulls in' \
	"the called elevator comes to the platform"
# a room with no line has nothing to call
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 call" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Nothing runs from here' "call needs a line to summon"
# a line set on a room the vehicle does not serve is refused
curl -sf -X POST -d "$SID1 @set #101.line=#$LIFT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 call" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'not on that line' "call refuses a platform off the route"
# clear the stray line so the lobby is not left pointing at a test vehicle
curl -sf -X POST -d "$SID1 @set #101.line=" http://localhost:$PORT/cmd >/dev/null

# --- M34: timed transit (see M34.md) ---
# A vehicle with a positive `transit` takes travel time between stops instead of
# hopping instantly: it departs, rides with its doors shut (moving=1), then
# arrives. While under way board/disembark/floor/call are refused. Reuse the
# lift (at the hub) and give it a travel time. Poll for moving=1 before probing
# so the doors-closed check does not race the arrival. "doors are closed" is new
# to this milestone; the arrival names Level Two, distinct from the earlier lift
# trip to Level Three.
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$LIFT.transit=2500" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 board elevator" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 floor 2" http://localhost:$PORT/cmd >/dev/null
_mi=0; while [ $_mi -lt 80 ]; do
	curl -sf -X POST -d "$SID1 @examine #$LIFT" http://localhost:$PORT/cmd >/dev/null
	grep -q 'moving = "1"' /tmp/smolmoo_p1.log && break
	sleep 0.05; _mi=$((_mi + 1))
done
curl -sf -X POST -d "$SID1 disembark" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'doors are closed' \
	"a vehicle in transit refuses disembark (doors closed)"
check_log /tmp/smolmoo_p1.log 'elevator arrives at Level Two' \
	"a timed vehicle arrives at its stop after the transit delay"
# it is stopped now: the rider steps off, and the travel time is cleared
curl -sf -X POST -d "$SID1 disembark" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'steps off the elevator' \
	"a rider can disembark once the timed vehicle has stopped"
curl -sf -X POST -d "$SID1 @set #$LIFT.transit=" http://localhost:$PORT/cmd >/dev/null

# --- M34 slice 2: spatial transit (a path with pass-through rooms) ---
# A train may carry a `path`: every room it traverses in order, the stops plus
# the pass-through rooms between them. It then walks one room per tick and really
# occupies each, so a rider sees the tunnel go by ("Through the window") and a
# player standing in that tunnel sees the train ("rushes past"). A pass-through
# room may also `observe` a room it can see into (slice 2b), so a rider glimpses
# a platform the subway skips and its people see it pass in the distance. Build a
# tunnel and platform beside the hub and run a short looping subway through it.
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
TUN=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$TUN.name=the tunnel" http://localhost:$PORT/cmd >/dev/null
# a platform the tunnel can see into (M34 slice 2b): the subway never enters it,
# but from the tunnel its riders glimpse it and the people on it see the subway
# pass in the distance.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
PLAT=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$PLAT.name=the platform" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$TUN.observe=$PLAT" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SUB=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SUB.name=subway" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.description=A subway car." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.vehicle=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.location=#$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.route=$HUB,$L2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.path=$HUB,$TUN,$L2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.dwell=250" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.transit=250" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.brain=#456" http://localhost:$PORT/cmd >/dev/null
# board at the hub while it is stopped, then wake it so it walks the path
curl -sf -X POST -d "$SID1 board subway" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$SUB" http://localhost:$PORT/cmd >/dev/null
# the rider sees the pass-through tunnel and then the arrival at the next stop
check_log /tmp/smolmoo_p1.log 'Through the window: the tunnel' \
	"a rider on a path train sees a pass-through room go by"
# from the tunnel the rider also glimpses the platform it can see into
check_log /tmp/smolmoo_p1.log 'In the distance: the platform' \
	"a rider glimpses a room the pass-through can see into"
check_log /tmp/smolmoo_p1.log 'subway arrives at Level Two' \
	"a path train carries its rider to the next stop"
# step off and stand in the tunnel; the looping subway soon rushes past
curl -sf -X POST -d "$SID1 disembark" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$TUN" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'subway rushes past' \
	"a player in a pass-through room sees the train go by"
# stand on the observed platform; the subway passes in the distance, not through
curl -sf -X POST -d "$SID1 @go #$PLAT" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'subway passes in the distance' \
	"a player in an observed room sees the train pass in the distance"
# quiet the subway so its loop does not bleed into later assertions
curl -sf -X POST -d "$SID1 @sleep #$SUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SUB.route=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null

# M34 slice 2c: an on-command elevator with a `path` walks toward the requested
# stop, showing the rooms it passes, instead of hopping straight there. Build a
# shaft (pass-through) between the hub and Level Three and a lift that walks it.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SHAFT=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SHAFT.name=the shaft" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
CAR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$CAR.name=cablecar" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.description=A glass car." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.vehicle=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.location=#$HUB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.route=$HUB,$L3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.path=$HUB,$SHAFT,$L3" http://localhost:$PORT/cmd >/dev/null
# no dwell: it is on command; a transit time makes it tick between rooms
curl -sf -X POST -d "$SID1 @set #$CAR.transit=250" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.brain=#456" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 board cablecar" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #$CAR" http://localhost:$PORT/cmd >/dev/null
# ask for the second stop (Level Three); it walks the shaft, not hops
curl -sf -X POST -d "$SID1 floor 2" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Through the window: the shaft' \
	"a path elevator shows the rooms it passes on the way"
check_log /tmp/smolmoo_p1.log 'cablecar arrives at Level Three' \
	"a path elevator walks to the requested stop and opens there"
curl -sf -X POST -d "$SID1 disembark" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @sleep #$CAR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$CAR.route=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null

# --- M24: export / merge CLI ---
# Use an isolated depot copy so the CLI tools do not race the running
# server on $DEPOT.
cp -a "$DEPOT"/* "$CDEP/" 2>/dev/null || true

# export logs to stderr; stdout carries only object data
SMOLMOO_DEPOT="$CDEP" _build/smolmoo export >/tmp/smolmoo_exp.txt 2>/dev/null
head -1 /tmp/smolmoo_exp.txt | grep -q '^#' \
	&& pass "export clean stdout" || fail "export clean stdout"
[ "$(grep -c '^#' /tmp/smolmoo_exp.txt)" -gt 0 ] \
	&& pass "export all" || fail "export all"

# a range selects only the requested ids (#100 proto, #101 lobby)
[ "$(SMOLMOO_DEPOT="$CDEP" _build/smolmoo export 100-101 2>/dev/null \
	| grep -c '^#')" -eq 2 ] \
	&& pass "export range" || fail "export range"

# merge renumbers input into a fresh range, rewriting references among
# the merged objects while leaving external references (#100, #0) alone
MAXID=$(SMOLMOO_DEPOT="$CDEP" _build/smolmoo export 2>/dev/null \
	| sed -n 's/^#\([0-9][0-9]*\).*/\1/p' | sort -n | tail -1)
B=$((MAXID + 1)); B2=$((MAXID + 2))
printf '#10 #100\nname=MergeRoom\nlink=#11\n\n#11 #10\ndest=#0\n' \
	>/tmp/smolmoo_mrg.txt
SMOLMOO_DEPOT="$CDEP" _build/smolmoo merge /tmp/smolmoo_mrg.txt >/dev/null 2>&1 \
	&& pass "merge run" || fail "merge run"
SMOLMOO_DEPOT="$CDEP" _build/smolmoo export "$B-$B2" 2>/dev/null \
	>/tmp/smolmoo_m2.txt
grep -q "link=#$B2" /tmp/smolmoo_m2.txt \
	&& pass "merge internal ref" || fail "merge internal ref"
grep -q "^#$B #100" /tmp/smolmoo_m2.txt \
	&& pass "merge external parent" || fail "merge external parent"
grep -q "^#$B2 #$B" /tmp/smolmoo_m2.txt \
	&& pass "merge internal parent" || fail "merge internal parent"
grep -q "dest=#0" /tmp/smolmoo_m2.txt \
	&& pass "merge external ref" || fail "merge external ref"

# --- M29: signed world history (@history / @rewind) ---
# Isolated instance on its own port and depot: @rewind disconnects every
# session, so it cannot run against the shared sessions used above.
HDEP=$(mktemp -d)
cp -a depot/* "$HDEP/" 2>/dev/null || true
SMOLMOO_PORT=7779 SMOLMOO_DEPOT="$HDEP" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_h.log &
HSRV=$!
waitgrep /tmp/smolmoo_h.log 'world signing on' || true
HINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_h.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7779/events > /tmp/smolmoo_h_p.log &
HP1=$!
waitgrep /tmp/smolmoo_h_p.log "^data: I" || true
HSID=$(grep -m1 "^data: I" /tmp/smolmoo_h_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$HSID create HWiz hpass $HINV" \
	http://localhost:7779/cmd >/dev/null
# Build two versions: create an object and save, twice.
curl -sf -X POST -d "$HSID @create #1" http://localhost:7779/cmd >/dev/null
curl -sf -X POST -d "$HSID @save" http://localhost:7779/cmd >/dev/null
curl -sf -X POST -d "$HSID @create #1" http://localhost:7779/cmd >/dev/null
curl -sf -X POST -d "$HSID @save" http://localhost:7779/cmd >/dev/null
curl -sf -X POST -d "$HSID @history" http://localhost:7779/cmd >/dev/null
check_log /tmp/smolmoo_h_p.log 'World history' \
	"@history lists the version chain"
# Rewind to the first version; the session is disconnected and the world
# is restored to that root.
curl -sf -X POST -d "$HSID @rewind 1" http://localhost:7779/cmd >/dev/null \
	|| true
check_log /tmp/smolmoo_h_p.log 'rolled back' \
	"@rewind notifies and disconnects sessions"
check_log /tmp/smolmoo_h.log 'rewound to seq 1' \
	"@rewind restores an earlier root"
kill $HSRV $HP1 2>/dev/null || true
rm -rf "$HDEP" "$HDEP.key" /tmp/smolmoo_h.log /tmp/smolmoo_h_p.log

# --- OLC-5: agent persistence across a world reload (boot-scan) ---
# Isolated instance, like the @rewind test. An awake agent is saved into the
# first version; the world is then changed and rewound back to it. On reload the
# boot scan must re-wake the agents the restored world marks awake, the same path
# that revives the living world after a server restart.
ADEP=$(mktemp -d)
cp -a depot/* "$ADEP/" 2>/dev/null || true
SMOLMOO_PORT=7783 SMOLMOO_DEPOT="$ADEP" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_a.log &
ASRV=$!
waitgrep /tmp/smolmoo_a.log 'world signing on' || true
AINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_a.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7783/events > /tmp/smolmoo_a_p.log &
AP1=$!
waitgrep /tmp/smolmoo_a_p.log "^data: I" || true
ASID=$(grep -m1 "^data: I" /tmp/smolmoo_a_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$ASID create AWiz apass $AINV" \
	http://localhost:7783/cmd >/dev/null
# Version 1 carries an awake agent (a beacon with the __ticker brain).
curl -sf -X POST -d "$ASID @create #300" http://localhost:7783/cmd >/dev/null
ABEACON=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_a_p.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$ASID @set #$ABEACON.brain=#452" http://localhost:7783/cmd >/dev/null
curl -sf -X POST -d "$ASID @wake #$ABEACON" http://localhost:7783/cmd >/dev/null
check_log /tmp/smolmoo_a_p.log "Woke #$ABEACON" "agent woken in the persistence instance"
curl -sf -X POST -d "$ASID @save" http://localhost:7783/cmd >/dev/null
# Version 2 is a later change, then rewind back to version 1's awake agent.
curl -sf -X POST -d "$ASID @create #1" http://localhost:7783/cmd >/dev/null
curl -sf -X POST -d "$ASID @save" http://localhost:7783/cmd >/dev/null
curl -sf -X POST -d "$ASID @rewind 1" http://localhost:7783/cmd >/dev/null || true
check_log /tmp/smolmoo_a.log 're-woke' \
	"a reload re-wakes agents the restored world marks awake"
kill $ASRV $AP1 2>/dev/null || true
rm -rf "$ADEP" "$ADEP.key" /tmp/smolmoo_a.log /tmp/smolmoo_a_p.log

# --- M30: depot garbage collection (@gc) ---
# Isolated instance: build several versions so there is superseded garbage,
# then collect it down to the newest version and confirm the world survives.
GDEP=$(mktemp -d)
cp -a depot/* "$GDEP/" 2>/dev/null || true
SMOLMOO_PORT=7780 SMOLMOO_DEPOT="$GDEP" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_g.log &
GSRV=$!
waitgrep /tmp/smolmoo_g.log 'world signing on' || true
GINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_g.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7780/events > /tmp/smolmoo_g_p.log &
GP1=$!
waitgrep /tmp/smolmoo_g_p.log "^data: I" || true
GSID=$(grep -m1 "^data: I" /tmp/smolmoo_g_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$GSID create GWiz gpass $GINV" \
	http://localhost:7780/cmd >/dev/null || true
for _n in 1 2 3 4; do
	curl -sf -X POST -d "$GSID @create #1" http://localhost:7780/cmd \
		>/dev/null || true
	curl -sf -X POST -d "$GSID @save" http://localhost:7780/cmd \
		>/dev/null || true
done
# Collect down to the newest version; older records and superseded pages go.
curl -sf -X POST -d "$GSID @gc 1" http://localhost:7780/cmd >/dev/null || true
check_log /tmp/smolmoo_g_p.log 'removed [1-9]' \
	"@gc collects superseded depot objects"
# The live world must still work after a sweep: look runs a MooScript verb,
# so its ELF blob was not collected.
curl -sf -X POST -d "$GSID look" http://localhost:7780/cmd >/dev/null || true
check_log /tmp/smolmoo_g_p.log 'Lobby' "world still serves after @gc"

# @fsck: a healthy depot reports no corruption, and flipping a byte in a
# stored object is detected on the next check.
curl -sf -X POST -d "$GSID @fsck" http://localhost:7780/cmd >/dev/null || true
check_log /tmp/smolmoo_g_p.log '0 corrupt' "@fsck passes on a clean depot"
GVICTIM=$(find "$GDEP" -type f | grep -E '/[0-9a-f]{2}/[0-9a-f]{64}$' | head -1)
printf 'X' | dd of="$GVICTIM" bs=1 seek=8 count=1 conv=notrunc 2>/dev/null
curl -sf -X POST -d "$GSID @fsck" http://localhost:7780/cmd >/dev/null || true
# A byte flip trips the hash check (corrupt); if the object was compressed it
# may fail to decode instead (unreadable). Either is a detection.
check_log /tmp/smolmoo_g_p.log '[1-9] corrupt\|[1-9] unreadable' \
	"@fsck detects a damaged object"

kill $GSRV $GP1 2>/dev/null || true
rm -rf "$GDEP" "$GDEP.key" /tmp/smolmoo_g.log /tmp/smolmoo_g_p.log

echo "---"
echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
