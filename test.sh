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
# a test area groups this rule (and the vending rule below) so an area-scoped
# @reset counts only them, not the seeded district rule the world now ships (M46a)
curl -sf -X POST -d "$SID1 @create #900" http://localhost:$PORT/cmd >/dev/null
TA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$TA.name=Test Area" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RULE.area=#$TA" http://localhost:$PORT/cmd >/dev/null
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
check_log /tmp/smolmoo_p1.log 'CP 10' "chargen banks the 10 CP creation budget"

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
# shows in the next round's prompt. Drawing it may not spend the turn (a foe out
# of reach), so drive the fight to that prompt with a deliberate hold each turn
# rather than waiting out the turn timeout, which can outlast a fixed poll. A
# hold only fires on the player's turn and is harmless otherwise.
_i=0
while [ $_i -lt 20 ]; do
	grep -q 'straggler at short range' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
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
curl -sf -X POST -d "$SID1 @set #$SRULE.area=#$TA" http://localhost:$PORT/cmd >/dev/null
# the raider rule from OLC-2 is still full, so this area pass stocks only the
# colas. Scope to the test area so the seeded district rule is not counted (M46a).
curl -sf -X POST -d "$SID1 @reset #$TA" http://localhost:$PORT/cmd >/dev/null
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
curl -sf -X POST -d "$SID1 @reset #$TA" http://localhost:$PORT/cmd >/dev/null
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

# --- M36c: the unlock roster (reflex cyberware, vigor spell) ---
# reflex is cyberware, +2 Defense, and fills the second graft slot beside dermal.
# Measure Defense before and after, under the cyber hook (the hook itself feeds
# Passive Defense, so the reading must not straddle a hook change).
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=50" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'Maneuvers: smartlink,dermal' || true
DB=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
curl -sf -X POST -d "$SID1 learn reflex" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You learn reflex' "learn acquires the reflex cyberware"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'Maneuvers: smartlink,dermal,reflex' || true
DA=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
GB=$(grep -oE 'Grit [0-9]+/[0-9]+' /tmp/smolmoo_p1.log | tail -1 | sed 's#.*/##')
[ "${DA:-0}" -eq "$(( ${DB:-0} + 2 ))" ] \
	&& pass "the learned reflex raises Defense by 2" \
	|| fail "the learned reflex raises Defense by 2"
# vigor is an awakened spell, +3 Max Grit. A character takes one hook at creation;
# switch to awakened to take it, then restore the cyber hook.
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=awakened" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 learn vigor" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You learn vigor' "learn acquires the vigor spell on the awakened hook"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'Maneuvers: smartlink,dermal,reflex,vigor' || true
GA=$(grep -oE 'Grit [0-9]+/[0-9]+' /tmp/smolmoo_p1.log | tail -1 | sed 's#.*/##')
[ "${GA:-0}" -eq "$(( ${GB:-0} + 3 ))" ] \
	&& pass "the learned vigor raises Max Grit by 3" \
	|| fail "the learned vigor raises Max Grit by 3"
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=cyber" http://localhost:$PORT/cmd >/dev/null

# --- M37a: jobs offered, accepted, tracked, abandoned ---
# A job giver is a builder object marked job=1 with a description and reward.
# Build one in the lobby and run the accept/track/abandon bookkeeping (the
# completion and payout arrive in M37b).
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
FIX=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$FIX.name=fixer" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.job_desc=Clear out the raider." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.job_cp=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.job_creds=50" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'fixer: Clear out the raider' "jobs lists a contract offered here"
curl -sf -X POST -d "$SID1 accept fixer" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'take the job from fixer' "accept takes an offered job"
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Your job: fixer (in progress)' "jobs shows the active contract"
curl -sf -X POST -d "$SID1 accept fixer" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'already have a job' "accept refuses a second job"
curl -sf -X POST -d "$SID1 abandon" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'abandon the job' "abandon drops the active job"
curl -sf -X POST -d "$SID1 abandon" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no job to abandon' "abandon needs an active job"

# --- M37b: bounty completion at the scene win, and turn-in payout ---
# Run this in a fresh isolated room: the lobby holds every world proto (including
# the downed #201 raider), so "raider" there would resolve to the proto, not our
# target. Move the fixer, spawn one clean raider, then run the loop.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
BR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BR.name=Contract Office" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.job_target=raider" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FIX.location=#$BR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$BR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #201" http://localhost:$PORT/cmd >/dev/null
RD=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$RD.location=#$BR" http://localhost:$PORT/cmd >/dev/null
# a plain @create instance inherits the proto's combat state; clear it so the
# raider is a live, fightable target
curl -sf -X POST -d "$SID1 @set #$RD.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RD.bp=12" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RD.wounds=0" http://localhost:$PORT/cmd >/dev/null
# put TestPlayer1 back in fighting shape after the earlier combat blocks
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=21" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 accept fixer" http://localhost:$PORT/cmd >/dev/null
# reporting before the target is down is refused
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'not done yet' "turnin refuses an unfinished job"
# the pistol is already wielded from the combat block (wield toggles, so do not
# re-wield); just top up its magazine for the fight
curl -sf -X POST -d "$SID1 reload" http://localhost:$PORT/cmd >/dev/null
fight_over raider || true
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'done; report back' "defeating the target completes the bounty"
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'fixer pays you 50 creds and 2 CP' "turnin pays the bounty reward"
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no job to report' "turnin needs an active job"
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M37c: courier goal (reach a destination) and standing reward ---
# Build an origin with an exit to a destination. A courier job completes on
# arriving at the destination (a hook in `go`) and pays a standing step. Turning
# in requires that completion, so the unique standing line proves the whole
# chain; the sheet then shows the new faction band.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
CA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$CA.name=Origin" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$CA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @dig north to Destination" http://localhost:$PORT/cmd >/dev/null
CB=$(grep -oE 'Dug #[0-9]+ to #[0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '#[0-9]+' | tail -1 | tr -d '#')
curl -sf -X POST -d "$SID1 @go #$CA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
BRK=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BRK.name=broker" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.job_desc=Run a package to the docks." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.job_dest=$CB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.job_cp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.job_creds=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.job_standing=couriers:2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRK.location=#$CA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 accept broker" http://localhost:$PORT/cmd >/dev/null
# travel to the destination: arriving there completes the courier goal
curl -sf -X POST -d "$SID1 go north" http://localhost:$PORT/cmd >/dev/null
# report back at the origin: pays creds, CP, and a standing step
curl -sf -X POST -d "$SID1 @go #$CA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'standing with couriers shifts' \
	"reaching the destination completes a courier job and turn-in pays standing"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'couriers' "the standing reward lands on the sheet"
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M38a: a job giver can gate a contract behind a minimum standing ---
# An optional job_min "faction:step" locks a contract from a player who does not
# rank for it. The player sits at Neutral (0) with the syndicate, below the gate,
# so jobs marks it locked and accept refuses; raising the standing opens it.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
HIRE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$HIRE.name=handler" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$HIRE.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$HIRE.job_desc=A trusted-only run." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$HIRE.job_cp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$HIRE.job_creds=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$HIRE.job_min=syndicate:2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$HIRE.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'locked: needs syndicate Friendly' "jobs marks a gated contract locked"
curl -sf -X POST -d "$SID1 accept handler" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'do not rank for that job' "accept refuses a contract below the gate"
# meet the gate, then accept succeeds
curl -sf -X POST -d "$SID1 @standing TestPlayer1 syndicate 2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 accept handler" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'take the job from handler' "accept takes a gated job once standing is met"
curl -sf -X POST -d "$SID1 abandon" http://localhost:$PORT/cmd >/dev/null

# --- M47a: a job giver can gate a contract behind a campaign flag (job_need) ---
# An optional job_need names a flag the character must carry on the sheet's flags
# list before the contract is offered or accepted, so one contract gates behind an
# earlier one. Proven on non-admin TestPlayer3: the flag gate is not masked by
# admin, but a player meets it by carrying the flag, so jobs marks it locked and
# accept refuses while the flag is absent, and both open once it is set.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
RECR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$RECR.name=recruiter" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RECR.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RECR.job_desc=A cleared-only run." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RECR.job_cp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RECR.job_creds=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RECR.job_need=arctoken" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$RECR.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'locked: requires arctoken' "jobs marks a chain contract locked without its flag"
curl -sf -X POST -d "$SID3 accept recruiter" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'not cleared for that job yet' "accept refuses a chain contract without its flag"
# grant the flag, then both the offer and the accept open
curl -sf -X POST -d "$SID1 @set #$P3SH.flags=arctoken" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 accept recruiter" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'take the job from recruiter' "accept takes a chain contract once the flag is set"
curl -sf -X POST -d "$SID3 abandon" http://localhost:$PORT/cmd >/dev/null

# --- M47b: turnin grants a campaign flag (job_grant), closing the chain loop ---
# A giver's job_grant names a flag that turnin adds to the sheet's flags list, so a
# later job_need contract opens. Build two givers in the lobby (the room a non-admin
# can reach): courierA grants keycard, courierB needs it. Before turnin courierB is
# locked; after, the flag lands (appended to the arctoken already there) and courierB
# accepts. Completion itself is M37b's concern, so admin sets job_done to isolate the
# grant. Proven on non-admin TestPlayer3, whose sheet the grant must reach.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
GA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$GA.name=courierA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GA.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GA.job_desc=A run that vouches for you." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GA.job_cp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GA.job_creds=10" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GA.job_grant=keycard" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GA.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
GB=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$GB.name=courierB" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GB.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GB.job_desc=A run only the cleared may take." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GB.job_creds=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GB.job_need=keycard" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GB.location=#101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @go #101" http://localhost:$PORT/cmd >/dev/null
# courierB is locked before the grant lands
curl -sf -X POST -d "$SID3 accept courierB" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'not cleared for that job yet' "the follow-up is locked before the grant"
# take courierA, mark it done (completion is M37b's concern), and turn it in
curl -sf -X POST -d "$SID3 accept courierA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P3SH.job_done=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'cleared as keycard' "turnin grants the campaign flag"
FLG=$(curl -s "http://localhost:$PORT/prop?obj=$P3SH&prop=flags&sid=$SID3" || true)
echo "$FLG" | grep -q 'keycard' && pass "the granted flag lands on the sheet" || fail "the granted flag lands on the sheet ($FLG)"
echo "$FLG" | grep -q 'arctoken' && pass "the grant appends without dropping an earlier flag" || fail "the grant appends ($FLG)"
# with the flag in hand, the follow-up now accepts
curl -sf -X POST -d "$SID3 accept courierB" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'take the job from courierB' "the follow-up accepts once the grant lands"
curl -sf -X POST -d "$SID3 abandon" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M47c: the seeded campaign chain in the waystation district ---
# The seeded fixer's scavenger bounty (#222) is step one, granting "vetted". A
# dispatcher (#225) in the bar offers a follow-up gated on vetted, granting
# "trusted" for more pay. A foreman (#226) in the office offers the payoff gated on
# trusted, targeting the respawning scavenger for the best reward. Drive the arc on
# non-admin TestPlayer3, whose sheet, standing, and flags are independent of the
# TestPlayer1 assertions the later M44b bounty makes on the same seeded fixer.
# Completion of each step is proven elsewhere (M37b bounty, M37c courier), so
# job_done is set to isolate the chain: the point here is that each step is locked
# until its predecessor is turned in, and the arc's final reward lands.
# TestPlayer3 (non-admin) cannot @go, so it walks the district through the seeded
# exits, the way a player does. It starts in the lobby (#101). Check each follow-up
# is locked before its prerequisite is earned: visit the office first (foreman needs
# trusted), then the bar (dispatcher needs vetted), before completing any step.
curl -sf -X POST -d "$SID3 go waystation" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 go office" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'locked: requires trusted' "the payoff is locked before the follow-up run"
curl -sf -X POST -d "$SID3 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 go bar" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'locked: requires vetted' "the follow-up run is locked before the entry bounty"
# step one: the fixer's scavenger bounty grants the first flag
curl -sf -X POST -d "$SID3 accept fixer" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P3SH.job_done=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'cleared as vetted' "the entry bounty grants the first chain flag"
# step two: with vetted, the dispatcher's run opens and grants the second flag
curl -sf -X POST -d "$SID3 accept dispatcher" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'take the job from dispatcher' "the follow-up opens once the entry bounty is done"
curl -sf -X POST -d "$SID1 @set #$P3SH.job_done=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'cleared as trusted' "the follow-up run grants the second chain flag"
# step three: with trusted, the foreman's payoff opens and pays the best reward
curl -sf -X POST -d "$SID3 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 go office" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 accept foreman" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'take the job from foreman' "the payoff opens once the follow-up is done"
curl -sf -X POST -d "$SID1 @set #$P3SH.job_done=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'foreman pays you 400 creds and 5 CP' "the arc's final reward lands"
curl -sf -X POST -d "$SID3 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID3 go lobby" http://localhost:$PORT/cmd >/dev/null

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

# --- M38b: a faction mob aggros a newcomer at Hostile standing ---
# A mob with a `faction` attacks on entry when the newcomer's standing with that
# faction is Hostile (-2) or worse, even without an aggro behavior token. Enter
# once at a friendly standing (greet only), then drop to Hostile and re-enter:
# the fresh 'turns on' proves the friendly entry did not open a fight (a running
# fight would make the second entry fall in silently instead).
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
HR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$HR.name=Hostile Reception" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
ENF=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$ENF.name=enforcer" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ENF.faction=raiders" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ENF.behavior=greet" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ENF.greeting=The enforcer eyes you." http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ENF.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ENF.location=#$HR" http://localhost:$PORT/cmd >/dev/null
# in good standing the faction mob only greets
curl -sf -X POST -d "$SID1 @standing TestPlayer1 raiders 1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HR" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'The enforcer eyes you' "a faction mob greets a newcomer in good standing"
# drop to Hostile, put the player back in fighting shape, and re-enter
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @standing TestPlayer1 raiders -2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=21" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HR" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'enforcer turns on' "a faction mob aggros a Hostile newcomer without an aggro token"
# tear the fight down and leave
curl -sf -X POST -d "$SID1 @set #$HR.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M38c: an NPC's disp offset shifts its effective standing ---
# A specific NPC can be warmer or colder than its faction. Build a vendor for the
# fringe and prove both directions through the price, which encodes the effective
# step: a positive disp lifts a Hostile-faction buyer into dealing range (Watched,
# +10 percent), and a negative disp cools a Friendly-faction one (Known, -10
# percent). The distinctive prices prove the effective step; a bare faction read
# would refuse the first sale outright.
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
FENCE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$FENCE.name=fence" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FENCE.faction=fringe" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FENCE.stim=10" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FENCE.location=#101" http://localhost:$PORT/cmd >/dev/null
# Hostile with the fringe, but the fence's +1 disp lifts it to Watched: it sells.
curl -sf -X POST -d "$SID1 @standing TestPlayer1 fringe -2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FENCE.disp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 buy stim from fence" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'buy a stim for 11 creds' "a positive NPC disp lifts a Hostile buyer into range"
# Friendly with the fringe, but the fence's -1 disp cools it to Known pricing.
curl -sf -X POST -d "$SID1 @standing TestPlayer1 fringe 2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$FENCE.disp=-1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 buy stim from fence" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'buy a stim for 9 creds' "a negative NPC disp cools a friendlier faction"

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

# --- M39a: short rest recovers BP over time in a safe room (Section 8) ---
# BP climbs back toward its maximum while a character sits in a `safe` room out
# of a fight, computed lazily on a sheet read. Tune the rest period short so the
# test does not wait on real hours. Distinct damaged values (8 safe, 7 unsafe)
# keep the negative check from matching the positive case's earlier log line.
curl -sf -X POST -d "$SID1 @set #0.rest_secs=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SAFE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SAFE.name=Safehouse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SAFE.safe=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=8" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.rest_since=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SAFE" http://localhost:$PORT/cmd >/dev/null
# the first read anchors the rest clock and shows the damaged BP
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'BP 8/21' "a damaged character reads below maximum before resting"
sleep 3
# after more than the rest period, a read pays out the recovery
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'BP 21/21' "a short rest in a safe room restores BP over time"
# an unsafe room accrues nothing: damage again, wait, and read once
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=7" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.rest_since=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$HUB" http://localhost:$PORT/cmd >/dev/null
sleep 3
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'BP 7/21' "an unsafe room does not recover BP"
curl -sf -X POST -d "$SID1 @set #0.rest_secs=7200" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=21" http://localhost:$PORT/cmd >/dev/null

# --- M39b: treat a wound at a clinic (Section 8) ---
# Wounds do not rest off; a living character mends at a clinic for a fee plus a
# Medicine or Cybertech check. Build a ward with a clinic object, and drive the
# refusal, success, and fee paths. The Medicine pool is set high enough that the
# check clears even on all-ones, so the success is deterministic.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
CLIN=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$CLIN.name=Trauma Ward" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$CLIN" http://localhost:$PORT/cmd >/dev/null
# no clinic object in the room yet
curl -sf -X POST -d "$SID1 treat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no clinic here' "treat needs a clinic in the room"
curl -sf -X POST -d "$SID1 @create #300" http://localhost:$PORT/cmd >/dev/null
DOC=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DOC.name=medic" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DOC.clinic=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DOC.treat_fee=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DOC.location=#$CLIN" http://localhost:$PORT/cmd >/dev/null
# unwounded: nothing to treat
curl -sf -X POST -d "$SID1 @set #$P1SH.wounds=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 treat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no wounds to treat' "treat refuses an unwounded patient"
# a wound, funds, and a strong Medicine skill: the mend clears one step
curl -sf -X POST -d "$SID1 @set #$P1SH.wounds=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_medicine=60" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.money=333" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 treat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'treats your wound (1 left)' "treat clears one wound step on success"
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Creds 293' "treat spends the clinic fee"
# too few creds: refused
curl -sf -X POST -d "$SID1 @set #$P1SH.money=10" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 treat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Treatment costs 40 creds; you have 10' "treat refuses an unaffordable fee"

# --- M39c: job cooldown and standing decay, the clock's deferred consumers ---
# A giver with a job_cd cannot be turned in and re-accepted back to back.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
JR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$JR.name=Runner Den" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$JR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
GJ=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$GJ.name=runner" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GJ.job=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GJ.job_creds=5" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GJ.job_cd=100" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GJ.location=#$JR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 accept runner" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.job_done=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'runner pays you 5 creds' "a cooldown job pays out on turn-in"
# the cooldown blocks an immediate re-accept
curl -sf -X POST -d "$SID1 accept runner" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'no fresh work for you yet' "a job cooldown blocks an immediate re-accept"
# clearing the stamp (as the cooldown lapsing would) reopens the contract
curl -sf -X POST -d "$SID1 @set #$P1SH.cd$GJ=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 accept runner" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Your job: runner (in progress)' "the contract reopens once the cooldown lapses"
curl -sf -X POST -d "$SID1 abandon" http://localhost:$PORT/cmd >/dev/null
# Standing decay: an idle reputation drifts one step toward Neutral per period.
# The wall-clock elapsed over a fixed sleep is not exact to the second, so a
# one-step assertion races the boundary. Instead decay far enough that the whole
# earned band is spent: with a one-second period and a three-second sleep at
# least three steps elapse, and cs_decay clamps at Neutral, so any extra step is
# absorbed and the result is deterministic.
curl -sf -X POST -d "$SID1 @set #$P1SH.standing=wolves:3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #0.decay_secs=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.decay_tick=0" http://localhost:$PORT/cmd >/dev/null
# the first read anchors the decay clock and shows the earned band
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'wolves Allied' "standing starts at its earned band"
sleep 3
# after several periods a read has drifted the standing all the way to Neutral
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'wolves Neutral' "idle standing decays toward Neutral"
curl -sf -X POST -d "$SID1 @set #0.decay_secs=604800" http://localhost:$PORT/cmd >/dev/null

# --- M40a: the Prone condition, the trip maneuver, and stand ---
# Deterministic parts first: the sheet lists an active condition, and stand
# clears it (a second stand then refuses, proving the clear).
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.prone=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Conditions: prone' "the sheet lists an active condition"
curl -sf -X POST -d "$SID1 stand" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'gets to their feet' "stand clears the prone condition"
curl -sf -X POST -d "$SID1 stand" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You are not prone' "stand refuses when not prone"
# In a fight, trip knocks an engaged foe prone; the tripped foe then fights at
# -1D, its attack line noting it prone. Isolated room and a weak, durable foe so
# nobody drops mid-test; the player's Brawl is stacked so the opposed trip wins.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
TRM=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$TRM.name=Sparring Cage" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$TRM" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
DUMMY=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DUMMY.name=dummy" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DUMMY.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DUMMY.bp=60" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DUMMY.mig=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DUMMY.agi=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DUMMY.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DUMMY.location=#$TRM" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.prone=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_brawl=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack dummy" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
# re-send the maneuver frequently so it lands inside the player's turn window
# (a slow poll can miss the turn entirely, since the turn is shorter than 3s)
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 trip dummy" http://localhost:$PORT/cmd >/dev/null
	grep -q 'sprawls prone' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'sprawls prone' "trip knocks an engaged foe prone"
# the tripped foe takes its turn Prone; its attack line notes the condition
check_log /tmp/smolmoo_p1.log 'fists (prone)' "a prone combatant's attack notes the condition"
# clean teardown: down the foe and confirm the fight ends, so its combat task
# does not linger and prompt the player into the next scenario
curl -sf -X POST -d "$SID1 @set #$DUMMY.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over dummy || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M40b: Stunned and Shaken, the stun and menace maneuvers ---
# Deterministic sheet display first.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.stunned=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.shaken=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Conditions: stunned, shaken' "the sheet lists stunned and shaken"
curl -sf -X POST -d "$SID1 @set #$P1SH.stunned=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.shaken=0" http://localhost:$PORT/cmd >/dev/null
# In a fight: stun skips a foe's turn; menace shakes a foe, whose attack is
# annotated and who then rallies. Isolated room, a weak durable foe, stacked
# Brawl and Command so the opposed maneuvers win.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR2=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR2.name=Drill Hall" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D2=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D2.name=goon" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.bp=60" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.mig=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.agi=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.wit=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D2.location=#$SR2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.prone=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_brawl=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_command=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack goon" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 stun goon" http://localhost:$PORT/cmd >/dev/null
	grep -q 'reels, stunned' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'reels, stunned' "stun lands a stunning blow"
check_log /tmp/smolmoo_p1.log 'is stunned and cannot act' "a stunned foe loses its turn"
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 menace goon" http://localhost:$PORT/cmd >/dev/null
	grep -q 'it is shaken' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'it is shaken' "menace leaves a foe shaken"
check_log /tmp/smolmoo_p1.log 'fists (shaken)' "a shaken foe's attack notes the condition"
# raise the foe's Wit so its next end-of-turn rally reliably succeeds
curl -sf -X POST -d "$SID1 @set #$D2.wit=30" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'shakes off the fear' "a shaken combatant rallies on a Wit save"
curl -sf -X POST -d "$SID1 @set #$D2.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over goon || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M40c: Ongoing damage (bleed), the rend maneuver and staunch; Crash on rest ---
# Deterministic parts first: the sheet lists bleeding, and staunch clears it out
# of a fight (a second staunch then refuses, proving the clear).
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bleed=3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Conditions: bleeding' "the sheet lists the bleeding condition"
curl -sf -X POST -d "$SID1 staunch" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'staunches the bleeding' "staunch ends the bleeding"
curl -sf -X POST -d "$SID1 staunch" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You are not bleeding' "staunch refuses when not bleeding"
# In a fight, rend sets an engaged foe bleeding; the bleed then ticks direct
# damage at the start of the foe's turn. Isolated room, a weak durable foe,
# stacked Brawl and Grit so the opposed rend wins.
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR3=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR3.name=Kill Room" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D3=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D3.name=savage" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D3.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D3.bp=60" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D3.mig=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D3.agi=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D3.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D3.location=#$SR3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bleed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_brawl=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack savage" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 rend savage" http://localhost:$PORT/cmd >/dev/null
	grep -q 'it is bleeding' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'it is bleeding' "rend sets an engaged foe bleeding"
# the bleeding foe takes direct damage at the start of its next turn
check_log /tmp/smolmoo_p1.log 'bleeds for 4' "Ongoing damage ticks at the start of the turn"
curl -sf -X POST -d "$SID1 @set #$D3.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over savage || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
# Crash clears on a full short rest (the M39 leftover). Damage BP and stack a
# Crash, rest to full in the safe room, and confirm the stack is gone.
curl -sf -X POST -d "$SID1 @set #0.rest_secs=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=8" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.crash=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.rest_since=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SAFE" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
sleep 3
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'BP 21/21' "a full rest restores BP before clearing Crash"
CR=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=crash&sid=$SID1")
[ "$CR" = "0" ] && pass "a full short rest clears the Crash stack" \
	|| fail "a full short rest clears the Crash stack (got '$CR')"
curl -sf -X POST -d "$SID1 @set #0.rest_secs=7200" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=21" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M41a: timed self-buffs (mana shield, dermal wire mesh) ---
# The read side first, fully deterministic: a live buff raises the derived stat
# read by cs_recalc and clears when it lapses. Grant both unlocks and fix Wit so
# the mesh bonus (Wit dice, here 3) is known. Baseline with both buffs off.
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=cyber" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=smartlink,dermal,reflex,vigor,shield,mesh" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wit=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.shield=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mesh=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
DB=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
SB=$(grep -oE 'Soak [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
# mana shield: +3 Passive Defense while its rounds remain
curl -sf -X POST -d "$SID1 @set #$P1SH.shield=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
DA=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
[ "${DA:-0}" -eq "$(( ${DB:-0} + 3 ))" ] \
	&& pass "a live mana shield raises Passive Defense by 3" \
	|| fail "a live mana shield raises Passive Defense by 3 (base $DB, buffed $DA)"
# when the buff lapses (rounds 0), the bonus is gone
curl -sf -X POST -d "$SID1 @set #$P1SH.shield=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
DL=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
[ "${DL:-0}" -eq "${DB:-0}" ] \
	&& pass "a lapsed mana shield restores Passive Defense" \
	|| fail "a lapsed mana shield restores Passive Defense (base $DB, lapsed $DL)"
# dermal wire mesh: +Wit dice (3) Soak while its rounds remain
curl -sf -X POST -d "$SID1 @set #$P1SH.mesh=3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
SA=$(grep -oE 'Soak [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
[ "${SA:-0}" -eq "$(( ${SB:-0} + 3 ))" ] \
	&& pass "a live dermal wire mesh raises Soak by the Wit dice" \
	|| fail "a live dermal wire mesh raises Soak by the Wit dice (base $SB, buffed $SA)"
curl -sf -X POST -d "$SID1 @set #$P1SH.mesh=0" http://localhost:$PORT/cmd >/dev/null
# learn gates the shield spell on the Awakened hook (the machinery, once here)
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=smartlink,dermal,reflex,vigor" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.cp=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 learn shield" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'needs the awakened hook' "learn gates the shield spell on the hook"
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=awakened" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 learn shield" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You learn shield' "learn acquires the shield spell on the awakened hook"
# In a fight, use activates the buff, and it counts down to nothing over rounds.
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=smartlink,dermal,reflex,vigor,shield,mesh" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR4=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR4.name=Warded Cell" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR4" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D4=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D4.name=husk" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D4.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D4.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D4.mig=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D4.agi=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D4.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D4.location=#$SR4" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.shield=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack husk" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 use shield" http://localhost:$PORT/cmd >/dev/null
	grep -q 'weaves a mana shield' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'weaves a mana shield' "use activates the mana shield in a fight"
# cycle turns with cheap attacks so cs_cond_tick counts the buff down to zero
_i=0
while [ $_i -lt 40 ]; do
	SV=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=shield&sid=$SID1")
	[ "$SV" = "0" ] && break
	curl -sf -X POST -d "$SID1 attack husk" http://localhost:$PORT/cmd >/dev/null
	sleep 0.2
	_i=$((_i + 1))
done
[ "$SV" = "0" ] && pass "a timed buff counts down and lapses over rounds" \
	|| fail "a timed buff counts down and lapses over rounds (shield left '$SV')"
curl -sf -X POST -d "$SID1 @set #$D4.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over husk || true
curl -sf -X POST -d "$SID1 @set #$P1SH.hook=cyber" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M41b: offensive unlocks (static shock) and the Suppressed condition ---
# Suppressed on the sheet, deterministic.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.prone=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.stunned=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.shaken=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bleed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.suppress=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Conditions: suppressed' "the sheet lists the suppressed condition"
curl -sf -X POST -d "$SID1 @set #$P1SH.suppress=0" http://localhost:$PORT/cmd >/dev/null
# Grant the offensive unlocks; stack Spellcasting so static shock always lands.
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=smartlink,dermal,reflex,vigor,shield,mesh,shock,suppress" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_spellcasting=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR5=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR5.name=Mana Sink" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR5" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D5=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D5.name=wraith" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D5.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D5.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D5.mig=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D5.agi=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D5.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D5.location=#$SR5" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack wraith" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
# suppressive fire pins the foes; the suppressed foe then attacks at -1D
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 use suppress" http://localhost:$PORT/cmd >/dev/null
	grep -q 'the enemy is pinned' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'the enemy is pinned' "suppressive fire pins the foes"
check_log /tmp/smolmoo_p1.log 'fists (suppressed)' "a suppressed foe's attack notes the condition"
# static shock is a Wit+Spellcasting spell attack for 4D
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 use shock wraith" http://localhost:$PORT/cmd >/dev/null
	grep -q 'with static shock for' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'with static shock for' "static shock lands a spell attack for damage"
curl -sf -X POST -d "$SID1 @set #$D5.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over wraith || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M41c: the aid action (biomedical injector) ---
# In a fight, inject restores BP to an ally in reach or to the caller. Grant the
# unlock, stack Medicine so the Moderate check always clears, and set a high
# Agility so the weak foe rarely lands a hit (keeping the caller's low BP stable).
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=smartlink,dermal,reflex,vigor,shield,mesh,shock,suppress,inject" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_medicine=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.agi=12" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR6=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR6.name=Trauma Bay" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR6" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D6=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D6.name=ghoul" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D6.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D6.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D6.mig=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D6.agi=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D6.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D6.location=#$SR6" http://localhost:$PORT/cmd >/dev/null
# a wounded ally standing by, not part of the fight
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
ALLY=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$ALLY.name=medtech" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ALLY.mig=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ALLY.bp=5" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ALLY.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$ALLY.location=#$SR6" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=5" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack ghoul" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
# inject a wounded ally: their BP climbs (they take no combat damage otherwise)
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 use inject medtech" http://localhost:$PORT/cmd >/dev/null
	grep -q 'biomedical injector into medtech' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'biomedical injector into medtech' "inject aids an ally in reach"
AB=$(curl -sf "http://localhost:$PORT/prop?obj=$ALLY&prop=bp&sid=$SID1")
[ "${AB:-0}" -gt 5 ] && pass "inject restores an ally's Body Points" \
	|| fail "inject restores an ally's Body Points (ally bp '$AB')"
# an enemy cannot be injected
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 use inject ghoul" http://localhost:$PORT/cmd >/dev/null
	grep -q "can't inject an enemy" /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log "can't inject an enemy" "inject refuses an enemy target"
# with no target, inject aids the caller
_i=0
while [ $_i -lt 40 ]; do
	curl -sf -X POST -d "$SID1 use inject" http://localhost:$PORT/cmd >/dev/null
	grep -q 'biomedical injector into themselves' /tmp/smolmoo_p1.log 2>/dev/null && break
	sleep 0.2
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'biomedical injector into themselves' "inject with no target aids the caller"
curl -sf -X POST -d "$SID1 @set #$D6.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over ghoul || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M42a: the reaction hook and Reflex Governor ---
# When a hit lands on the player, the Reflex Governor reaction spends 2 Grit and
# a reaction to roll Wit+Cybertech vs the attack total, cutting the hit by the
# player's Wit dice on a success. Stack Cybertech so the save always clears (a
# fixed -3, from Wit 9), give the foe a huge Might so its brawl always hits, drop
# the player's Soak to zero so the hit lands for damage, and drive the fight with
# holds so the foe's attack comes promptly.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=governor" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wit=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_cybertech=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR7=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR7.name=Reflex Range" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR7" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D7=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D7.name=brute" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D7.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D7.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D7.mig=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D7.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D7.location=#$SR7" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack brute" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	grep -q 'governor -3' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'governor -3' "Reflex Governor cuts an incoming hit on a reaction"
curl -sf -X POST -d "$SID1 @set #$D7.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over brute || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M42b: Riposte and Emergency Defibrillator ---
# Riposte: when a foe misses the player's Passive Defense by 4 or more, the player
# spends a reaction and 1 Grit for a free strike back. Give the foe a hopeless
# attack pool and the player a high Passive Defense so every foe swing misses
# wide, then drive the fight with holds until the riposte fires.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=riposte" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.agi=12" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR8=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR8.name=Parry Pit" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR8" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D8=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D8.name=flailer" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D8.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D8.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D8.mig=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D8.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D8.location=#$SR8" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack flailer" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	grep -q 'riposte!' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'riposte!' "Riposte strikes back when a foe misses by a wide margin"
curl -sf -X POST -d "$SID1 @set #$D8.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over flailer || true
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# Emergency Defibrillator: a hit that would drop the player to 0 BP leaves them at
# 1 instead, once, setting a burnout flag. Put the player at 1 BP with no Soak and
# send a hard-hitting foe; the first landing blow triggers the defibrillator.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=defib" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.defib_used=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR9=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR9.name=Flatline Ward" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SR9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
D9=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$D9.name=reaper" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D9.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D9.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D9.mig=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D9.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$D9.location=#$SR9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack reaper" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	grep -q 'defibrillator jolts' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
curl -sf -X POST -d "$SID1 @set #$D9.downed=1" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'defibrillator jolts' "Emergency Defibrillator saves a lethal hit at 1 BP"
DU=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=defib_used&sid=$SID1")
[ "$DU" = "1" ] && pass "the defibrillator burns out after firing" \
	|| fail "the defibrillator burns out after firing (defib_used '$DU')"
fight_over reaper || true
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M42c: Empathic Aegis and Kinetic Absorbers ---
# Kinetic Absorbers, deterministic: a passive +1 Passive Defense at the cost of
# -1 Max Grit, read live by cs_recalc. Capture the baseline, then grant it.
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
DB=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
GB=$(grep -oE 'Grit [0-9]+/[0-9]+' /tmp/smolmoo_p1.log | tail -1 | sed 's#.*/##')
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=kinetic" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 sheet" http://localhost:$PORT/cmd >/dev/null
DA=$(grep -oE 'Defense [0-9]+' /tmp/smolmoo_p1.log | tail -1 | grep -oE '[0-9]+')
GA=$(grep -oE 'Grit [0-9]+/[0-9]+' /tmp/smolmoo_p1.log | tail -1 | sed 's#.*/##')
[ "${DA:-0}" -eq "$(( ${DB:-0} + 1 ))" ] \
	&& pass "Kinetic Absorbers raise Passive Defense by 1" \
	|| fail "Kinetic Absorbers raise Passive Defense by 1 (base $DB, with $DA)"
[ "${GA:-0}" -eq "$(( ${GB:-0} - 1 ))" ] \
	&& pass "Kinetic Absorbers lower Max Grit by 1" \
	|| fail "Kinetic Absorbers lower Max Grit by 1 (base $GB, with $GA)"
# Empathic Aegis: when the player is hit, a bystanding ally holding the spell
# spends a reaction and 1 Grit to add its Charm dice to the player's Soak for the
# hit. Zero the player's Soak, station a high-Charm ally in the room, and send a
# hard hitter; the ally's aegis softens each landing blow.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SRA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SRA.name=Warded Hall" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$SRA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
DA2=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DA2.name=slayer" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DA2.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DA2.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DA2.mig=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DA2.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DA2.location=#$SRA" http://localhost:$PORT/cmd >/dev/null
# the bystanding ally: not in the fight, holds aegis, high Charm, reactions ready
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
GUARD=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$GUARD.name=guardian" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.maneuvers=aegis" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.cha=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.react_left=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$GUARD.location=#$SRA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack slayer" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
_i=0
while [ $_i -lt 40 ]; do
	grep -q 'aegis' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 hold" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'aegis' "Empathic Aegis shields a hit from a bystanding ally"
curl -sf -X POST -d "$SID1 @set #$DA2.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over slayer || true
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M43a: the one-line status readout ---
# `status` prints the HP and Grit bars, the wound ladder, and any active
# conditions, a glance without the full sheet. Deterministic: set known vitals in
# the (unsafe) lobby so no short-rest payout changes them, then read the line.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=9" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wounds=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.prone=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.stunned=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.shaken=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bleed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.suppress=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 status" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'HP \[' "status shows the HP bar"
check_log /tmp/smolmoo_p1.log 'WOUNDS o\.\.' "status shows the wound ladder"
check_log /tmp/smolmoo_p1.log '(prone)' "status shows the active conditions"
# the short alias resolves to the same readout
curl -sf -X POST -d "$SID1 @set #$P1SH.prone=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 st" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'GRIT \[' "the st alias prints the status line"
curl -sf -X POST -d "$SID1 @set #$P1SH.wounds=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null

# --- M43b: in-game help for the game commands ---
# The ChromeSix verbs are seeded as help topics on #0.help, grouped by area, with
# a 'commands' index. A player can now look them up from inside the game.
curl -sf -X POST -d "$SID1 help commands" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'by area' "help commands lists the game command areas"
curl -sf -X POST -d "$SID1 help combat" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'push spends Grit' "a game-command help topic prints in-game"
curl -sf -X POST -d "$SID1 help maneuvers" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'impose or clear a' "the maneuvers help topic prints in-game"

# --- M43c: new-player onboarding ---
# A fresh account has a bare sheet (chargen sets a hook; absence marks it unmade).
# Log one in and confirm it is greeted with orientation, that status guides it to
# chargen rather than printing a blank readout, and that the start topic exists.
curl -sf -X POST -d "$SID1 @invite new" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'New code:' || true
INVITE4=$(grep -o 'New code: [A-Z0-9-]*' /tmp/smolmoo_p1.log | tail -1 | sed 's/New code: //')
curl -sN http://localhost:$PORT/events > /tmp/smolmoo_p4.log &
P4=$!
waitgrep /tmp/smolmoo_p4.log "^data: I" || true
SID4=$(grep -m1 "^data: I" /tmp/smolmoo_p4.log | sed 's/^data: I//')
curl -sf -X POST -d "$SID4 create TestPlayer4 pass4 $INVITE4" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p4.log 'data: +' "create acct 4"
check_log /tmp/smolmoo_p4.log 'set up your character' "a fresh login is greeted with onboarding"
curl -sf -X POST -d "$SID4 status" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p4.log "haven't set up your character" "status guides an uncreated character to chargen"
curl -sf -X POST -d "$SID4 help start" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p4.log 'New here' "the start help topic orients a new player"
kill $P4 2>/dev/null || true

# --- M44a: the starter district hub and its services ---
# Drive the seeded waystation (not test-built objects): travel in from the lobby,
# buy from the seeded vendor, treat a wound at the seeded clinic, and rest in the
# seeded safe bunk. This exercises the real seed ids end to end.
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null
# earlier tests left Might at 0 (carry cap 0, so any load blocks movement) and a
# thin Medicine pool; restore a workable build for the walkthrough.
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_medicine=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.money=500" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wounds=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go waystation" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Waystation Concourse' "the lobby connects to the seeded district"
curl -sf -X POST -d "$SID1 go supply" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 list" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'vest -- 80 creds' "the seeded vendor lists priced stock"
curl -sf -X POST -d "$SID1 buy vest" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'You buy the vest' "a player buys from the seeded vendor"
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go medbay" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 treat" http://localhost:$PORT/cmd >/dev/null
WD=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=wounds&sid=$SID1")
[ "$WD" = "0" ] && pass "the seeded clinic treats a wound" \
	|| fail "the seeded clinic treats a wound (wounds '$WD')"
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go bunks" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #0.rest_secs=2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=5" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.rest_since=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 status" http://localhost:$PORT/cmd >/dev/null
sleep 3
curl -sf -X POST -d "$SID1 status" http://localhost:$PORT/cmd >/dev/null
RB=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=bp&sid=$SID1")
[ "${RB:-0}" -gt 5 ] && pass "the seeded bunk is a safe room that restores BP" \
	|| fail "the seeded bunk is a safe room that restores BP (bp '$RB')"
curl -sf -X POST -d "$SID1 @set #0.rest_secs=7200" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M44b: the fixer and the bounty loop ---
# The full seeded loop: a reset spawns the scavenger in the cargo bay, the fixer
# in the bar offers the contract, and defeating the target completes it for pay
# and standing. Drive the seeded ids with real movement between the rooms.
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_brawl=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wielded=305" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.money=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.job_giver=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.job_done=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.standing=" http://localhost:$PORT/cmd >/dev/null
# accept the contract at the bar
curl -sf -X POST -d "$SID1 go waystation" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go bar" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Clear the scavenger' "the seeded fixer offers a contract"
curl -sf -X POST -d "$SID1 accept fixer" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'take the job from fixer' "the fixer's contract is accepted"
# travel to the cargo bay and defeat the target
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go cargobay" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Cargo Bay' "the cargo bay is reachable from the hub"
curl -sf -X POST -d "$SID1 attack scavenger" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
# fight_over drives the turn loop with attacks and holds until the scavenger is
# down; downing the bounty target sets job_done through mark_job_done.
fight_over scavenger || true
JD=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=job_done&sid=$SID1")
[ "$JD" = "1" ] && pass "defeating the bounty target completes the job" \
	|| fail "defeating the bounty target completes the job (job_done '$JD')"
# report back and collect the reward
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go bar" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'pays you 120 creds and 2 CP' "turn-in pays the seeded reward"
STG=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=standing&sid=$SID1")
case "$STG" in *dockers:1*) pass "the contract raises faction standing" ;;
	*) fail "the contract raises faction standing (standing '$STG')" ;; esac
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M44c: a faction presence and a living NPC ---
# The dockers hold an office off the concourse. A handler there offers a contract
# gated behind dockers standing (job_min), and a seeded guard reacts to who walks
# in: it greets a newcomer, roves its route when woken (a brain), and aggros one
# the dockers count Hostile. Drive the seeded ids to prove each consequence.
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.money=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.job_giver=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.job_done=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @standing TestPlayer1 dockers 0" http://localhost:$PORT/cmd >/dev/null
# the seeded guard greets a newcomer entering the office (reactive on_enter)
curl -sf -X POST -d "$SID1 go waystation" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go office" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'Dockers Office' "the office is reachable from the concourse"
check_log /tmp/smolmoo_p1.log 'guard sizes you up and grunts' "the seeded guard greets a newcomer"
# the handler's contract is gated: at Neutral it is locked and refused
curl -sf -X POST -d "$SID1 jobs" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'locked: needs dockers Known' "a job_min contract shows locked below the band"
curl -sf -X POST -d "$SID1 accept handler" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'do not rank for that job' "the gate refuses accept below the band"
# earn the band; the gate opens and the courier contract runs and pays out
curl -sf -X POST -d "$SID1 @standing TestPlayer1 dockers 1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 accept handler" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'take the job from handler' "the gate opens once the band is earned"
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go cargobay" http://localhost:$PORT/cmd >/dev/null
JD=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=job_done&sid=$SID1")
[ "$JD" = "1" ] && pass "reaching the courier destination completes the gated job" \
	|| fail "reaching the courier destination completes the gated job (job_done '$JD')"
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go office" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 turnin" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'pays you 90 creds and 1 CP' "the gated courier pays on turn-in"
# the seeded guard carries a brain: woken, it roves its seeded route (126,120),
# announcing the departure to the office the player is standing in
curl -sf -X POST -d "$SID1 @set #224.dwell=200" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @wake #224" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'guard leaves' "the seeded guard roves its route when woken (brain)"
# stop and repark it so it holds the office for the aggro check, and clear the
# persistent awake flag so a later boot-scan does not re-wake it (stale depot)
curl -sf -X POST -d "$SID1 @sleep #224" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #224.behavior=greet" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #224.location=#126" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #224.dwell=600000" http://localhost:$PORT/cmd >/dev/null
# a Hostile newcomer draws the guard's aggro (faction disposition on a seeded NPC)
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @standing TestPlayer1 dockers -2" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go concourse" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 go office" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log 'guard turns on' "the seeded guard aggros a Hostile newcomer via disposition"
# tear the fight down, restore standing, and leave
curl -sf -X POST -d "$SID1 @set #126.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @standing TestPlayer1 dockers 1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M45a: the initiative unlock (Wired Reflexes) ---
# Wired Reflexes adds a flat bonus to the initiative roll, so its holder leads the
# turn order. With both combatants at Wit 3 (one die each), the foe rolls 1-6 and
# the wired player rolls 7-12, so the player always wins initiative. Have the FOE
# open the fight (aggro), so the opening roster is foe-first; if the sort put the
# wired player first, initiative reordered it. Read the written roster and check.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=wired" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wit=3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
IR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$IR.name=Init Range" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
DI=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DI.name=slowfoe" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DI.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DI.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DI.wit=3" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DI.behavior=aggro" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DI.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DI.location=#$IR" http://localhost:$PORT/cmd >/dev/null
# enter: the aggro foe opens the fight with roster "foe,player" (foe #DI first).
# If initiative reorders it so the foe is last, the wired player won and leads.
curl -sf -X POST -d "$SID1 @go #$IR" http://localhost:$PORT/cmd >/dev/null
_i=0
while [ $_i -lt 40 ]; do
	ROST=$(curl -sf "http://localhost:$PORT/prop?obj=$IR&prop=cb_roster&sid=$SID1")
	[ "${ROST##*,}" = "$DI" ] && break
	sleep 0.1
	_i=$((_i + 1))
done
[ "${ROST##*,}" = "$DI" ] && pass "Wired Reflexes leads the initiative order" \
	|| fail "Wired Reflexes leads the initiative order (roster '$ROST', foe '$DI')"
curl -sf -X POST -d "$SID1 @set #$DI.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over slowfoe || true
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M45b: the marking and debuff unlocks with Exposed ---
# Two on-your-turn cyberware unlocks apply a tracked condition to a target:
# Tactical Co-Processor (coproc) leaves it Exposed (-2 Passive Defense) and
# Threat-Assessment Optics (optics) marks it so the marker's attacks gain +1D.
# Both are read in cs_attack_resolve. Drive a fight and, on the player's turns,
# apply each (a use off-turn is a no-op), then attack to see both annotations.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=coproc,optics" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_brawl=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.wielded=305" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
MR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$MR.name=Mark Range" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$MR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
DM=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DM.name=dummy" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DM.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DM.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DM.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DM.location=#$MR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack dummy" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
# coproc on the player's turn Exposes the target (a use off-turn is a no-op)
_i=0
while [ $_i -lt 40 ]; do
	EX=$(curl -sf "http://localhost:$PORT/prop?obj=$DM&prop=exposed&sid=$SID1" || true)
	[ "$EX" = "1" ] && break
	curl -sf -X POST -d "$SID1 use coproc dummy" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
[ "$EX" = "1" ] && pass "Tactical Co-Processor leaves the target Exposed" \
	|| fail "Tactical Co-Processor leaves the target Exposed (exposed '$EX')"
# optics on the player's turn marks the target with the marker's sheet id
_i=0
while [ $_i -lt 40 ]; do
	MK=$(curl -sf "http://localhost:$PORT/prop?obj=$DM&prop=marked_by&sid=$SID1" || true)
	[ "$MK" = "$P1SH" ] && break
	curl -sf -X POST -d "$SID1 use optics dummy" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
[ "$MK" = "$P1SH" ] && pass "Threat-Assessment Optics marks the target" \
	|| fail "Threat-Assessment Optics marks the target (marked_by '$MK')"
# a player attack now reads both conditions in the resolution line
_i=0
while [ $_i -lt 40 ]; do
	grep -q 'target exposed' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 attack dummy" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'target exposed' "cs_attack_resolve reads Exposed on the target"
check_log /tmp/smolmoo_p1.log '(marked)' "cs_attack_resolve reads the mark for the marker"
curl -sf -X POST -d "$SID1 @set #$DM.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over dummy || true
curl -sf -X POST -d "$SID1 @set #$P1SH.maneuvers=" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M45c: the remaining conditions (Blinded and Held) ---
# Two inflictor maneuvers set a tracked condition on a foe: flash leaves it
# Blinded (-3D on its attacks, wearing off after a round via cs_cond_tick) and
# grapple leaves it Held (it cannot change bands or flee until it breaks free).
# The reads live in cs_attack_resolve and the movement path; break clears Held.
# Stack the player's pools so both maneuvers always win the opposed roll.
curl -sf -X POST -d "$SID1 @set #101.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.agi=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.mig=20" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_firearms=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.sk_brawl=40" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.grit=30" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.bp=999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.blind=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.held=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
CR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$CR.name=Cond Range" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #$CR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
DG=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$DG.name=goon" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.bp=9999" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.mig=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.agi=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.band=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.downed=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$DG.location=#$CR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 attack goon" http://localhost:$PORT/cmd >/dev/null
waitgrep /tmp/smolmoo_p1.log 'combat begins' || true
# flash Blinds the foe (poll: a maneuver off-turn is a no-op)
_i=0
while [ $_i -lt 40 ]; do
	BL=$(curl -sf "http://localhost:$PORT/prop?obj=$DG&prop=blind&sid=$SID1" || true)
	[ "${BL:-0}" -gt 0 ] 2>/dev/null && break
	curl -sf -X POST -d "$SID1 flash goon" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
[ "${BL:-0}" -gt 0 ] 2>/dev/null && pass "flash leaves the foe Blinded" \
	|| fail "flash leaves the foe Blinded (blind '$BL')"
# grapple Holds the foe
_i=0
while [ $_i -lt 40 ]; do
	HD=$(curl -sf "http://localhost:$PORT/prop?obj=$DG&prop=held&sid=$SID1" || true)
	[ "$HD" = "1" ] && break
	curl -sf -X POST -d "$SID1 grapple goon" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
[ "$HD" = "1" ] && pass "grapple leaves the foe Held" \
	|| fail "grapple leaves the foe Held (held '$HD')"
# a Blinded attacker's roll is penalized: blind the player and read the annotation
curl -sf -X POST -d "$SID1 @set #$P1SH.blind=9" http://localhost:$PORT/cmd >/dev/null
_i=0
while [ $_i -lt 40 ]; do
	grep -q '(blinded)' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 attack goon" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log '(blinded)' "cs_attack_resolve penalizes a Blinded attacker"
# a Held combatant cannot change bands; break frees it
curl -sf -X POST -d "$SID1 @set #$P1SH.blind=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.held=1" http://localhost:$PORT/cmd >/dev/null
_i=0
while [ $_i -lt 40 ]; do
	grep -q 'held fast' /tmp/smolmoo_p1.log 2>/dev/null && break
	curl -sf -X POST -d "$SID1 retreat" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
check_log /tmp/smolmoo_p1.log 'held fast' "Held blocks a band change until it breaks free"
_i=0
while [ $_i -lt 40 ]; do
	HP=$(curl -sf "http://localhost:$PORT/prop?obj=$P1SH&prop=held&sid=$SID1" || true)
	[ "$HP" = "0" ] && break
	curl -sf -X POST -d "$SID1 break" http://localhost:$PORT/cmd >/dev/null
	sleep 0.3
	_i=$((_i + 1))
done
[ "$HP" = "0" ] && pass "break frees a Held combatant" \
	|| fail "break frees a Held combatant (held '$HP')"
curl -sf -X POST -d "$SID1 @set #$DG.downed=1" http://localhost:$PORT/cmd >/dev/null
fight_over goon || true
curl -sf -X POST -d "$SID1 @set #$P1SH.blind=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$P1SH.held=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @go #101" http://localhost:$PORT/cmd >/dev/null

# --- M46a: boot reconcile and the seeded reset rule ---
# The world ships reset rule #921 (proto #221, room #125, area #920) for the
# cargo-bay scavenger, reconciled at boot so the bay starts populated with no
# wizard @reset -- the M44b bounty fought that boot-spawned instance, which is
# proof the boot pass ran. That fight left the scavenger downed; an area-scoped
# reset counts only the district rule (not the OLC test rules) and respawns it.
curl -sf -X POST -d "$SID1 @reset #920" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '1 rule(s), 1 spawned' "an area reset respawns the seeded district foe"
# idempotent now the bay is full again
curl -sf -X POST -d "$SID1 @reset #920" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p1.log '1 rule(s), 0 spawned' "the area reset is idempotent while full"

# --- M46b: periodic auto-reset (the living district) ---
# An area carrying an `autoreset` interval (seconds) repopulates its rooms on its
# own, no wizard @reset. The seeded district #920 is wired for it; the mechanism
# is proven on an isolated area with a short interval so the test runs in seconds.
AR=$(curl -sf "http://localhost:$PORT/prop?obj=920&prop=autoreset&sid=$SID1" || true)
[ "$AR" = "300" ] && pass "the seeded district is wired for auto-reset" \
	|| fail "the seeded district is wired for auto-reset (autoreset '$AR')"
# an isolated auto-reset area: a short interval, one room, one proto, one rule
curl -sf -X POST -d "$SID1 @create #900" http://localhost:$PORT/cmd >/dev/null
BA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BA.name=Bloom Area" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BA.autoreset=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
BR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BR.name=Bloom Room" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
BP=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BP.name=sprout" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BP.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #910" http://localhost:$PORT/cmd >/dev/null
BRULE=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$BRULE.room=#$BR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRULE.proto=#$BP" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRULE.count=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$BRULE.area=#$BA" http://localhost:$PORT/cmd >/dev/null
# populate once, then down the instance so the room is short a live sprout
curl -sf -X POST -d "$SID1 @reset #$BA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #$BR" http://localhost:$PORT/cmd >/dev/null
SPR=$(grep -oE "#[0-9]+  sprout" /tmp/smolmoo_p1.log | grep -oE '[0-9]+' | sort -n | tail -1)
curl -sf -X POST -d "$SID1 @set #$SPR.downed=1" http://localhost:$PORT/cmd >/dev/null
# No command drives it: the periodic sweep reconciles the area on its own and
# spawns a replacement, which gets a fresh (higher) id than the downed one. Poll
# @contents for a sprout id above SPR -- proof the timer, not a manual @reset, ran.
_i=0
while [ $_i -lt 15 ]; do
	sleep 1
	curl -sf -X POST -d "$SID1 @contents #$BR" http://localhost:$PORT/cmd >/dev/null
	MX=$(grep -oE "#[0-9]+  sprout" /tmp/smolmoo_p1.log | grep -oE '[0-9]+' | sort -n | tail -1)
	[ "${MX:-0}" -gt "$SPR" ] 2>/dev/null && break
	_i=$((_i + 1))
done
[ "${MX:-0}" -gt "$SPR" ] 2>/dev/null && pass "an autoreset area respawns its foe on its own" \
	|| fail "an autoreset area respawns its foe on its own (max sprout '$MX', was '$SPR')"
curl -sf -X POST -d "$SID1 @set #$BA.autoreset=0" http://localhost:$PORT/cmd >/dev/null

# --- M46c: respawn safety and the living loop ---
# Respawn never pops a foe into a room with a fight in progress: a reconcile skips
# such a room and fills it once the scene clears. Build an isolated area with a
# rule, mark the room in a fight, and confirm nothing spawns until it clears.
curl -sf -X POST -d "$SID1 @create #900" http://localhost:$PORT/cmd >/dev/null
SA=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SA.name=Safe Area" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #100" http://localhost:$PORT/cmd >/dev/null
SR=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SR.name=Safe Room" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #200" http://localhost:$PORT/cmd >/dev/null
SP=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SP.name=creep" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SP.grade=tough" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @create #910" http://localhost:$PORT/cmd >/dev/null
SRULE2=$(grep -o 'Created #[0-9]*' /tmp/smolmoo_p1.log | tail -1 | grep -o '[0-9]*')
curl -sf -X POST -d "$SID1 @set #$SRULE2.room=#$SR" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SRULE2.proto=#$SP" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SRULE2.count=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @set #$SRULE2.area=#$SA" http://localhost:$PORT/cmd >/dev/null
# a fight is running in the room: the reconcile must skip it
curl -sf -X POST -d "$SID1 @set #$SR.cb_active=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @reset #$SA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #$SR" http://localhost:$PORT/cmd >/dev/null
if grep -qE "#[0-9]+  creep" /tmp/smolmoo_p1.log; then
	fail "a reconcile skips a room with a fight in progress"
else
	pass "a reconcile skips a room with a fight in progress"
fi
# clear the scene; the next reconcile fills the room
curl -sf -X POST -d "$SID1 @set #$SR.cb_active=0" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @reset #$SA" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #$SR" http://localhost:$PORT/cmd >/dev/null
if grep -qE "#[0-9]+  creep" /tmp/smolmoo_p1.log; then
	pass "the reconcile fills the room once the scene clears"
else
	fail "the reconcile fills the room once the scene clears"
fi

# the living loop on the district: the scavenger is reset-managed, so downing it
# and reconciling brings back a fresh one that is still the bounty target by name.
curl -sf -X POST -d "$SID1 @reset #920" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #125" http://localhost:$PORT/cmd >/dev/null
SCV=$(grep -oE "#[0-9]+  scavenger" /tmp/smolmoo_p1.log | grep -oE '[0-9]+' | sort -n | tail -1)
RSP=$(curl -sf "http://localhost:$PORT/prop?obj=$SCV&prop=reset_spawn&sid=$SID1" || true)
[ "$RSP" = "1" ] && pass "the district foe is reset-managed" \
	|| fail "the district foe is reset-managed (reset_spawn '$RSP')"
curl -sf -X POST -d "$SID1 @set #$SCV.downed=1" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @reset #920" http://localhost:$PORT/cmd >/dev/null
curl -sf -X POST -d "$SID1 @contents #125" http://localhost:$PORT/cmd >/dev/null
FRESH=$(grep -oE "#[0-9]+  scavenger" /tmp/smolmoo_p1.log | grep -oE '[0-9]+' | sort -n | tail -1)
[ "${FRESH:-0}" -gt "$SCV" ] 2>/dev/null && pass "a downed district foe respawns as a fresh bounty target" \
	|| fail "a downed district foe respawns as a fresh bounty target (fresh '$FRESH', was '$SCV')"

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

# --- M48a: an instance re-bases a history chain it did not sign ---
# A depot can carry a history head signed by a different key (a stale depot from an
# earlier run, or a restored backup). An instance builds only on history in its own
# topic, so it re-bases such a head to a fresh self-signed chain rather than appending
# across a topic seam that would dead-end @rewind. First, sign a depot under one key:
# a throwaway server builds a two-version chain, which writes a key-A head into it.
RBA=$(mktemp -d)
cp -a depot/* "$RBA/" 2>/dev/null || true
SMOLMOO_PORT=7781 SMOLMOO_DEPOT="$RBA" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_rba.log &
RBASRV=$!
waitgrep /tmp/smolmoo_rba.log 'world signing on' || true
RBAINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_rba.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7781/events > /tmp/smolmoo_rba_p.log &
RBAP=$!
waitgrep /tmp/smolmoo_rba_p.log "^data: I" || true
RBASID=$(grep -m1 "^data: I" /tmp/smolmoo_rba_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$RBASID create AWiz apass $RBAINV" \
	http://localhost:7781/cmd >/dev/null
curl -sf -X POST -d "$RBASID @create #1" http://localhost:7781/cmd >/dev/null
curl -sf -X POST -d "$RBASID @save" http://localhost:7781/cmd >/dev/null
curl -sf -X POST -d "$RBASID @create #1" http://localhost:7781/cmd >/dev/null
curl -sf -X POST -d "$RBASID @save" http://localhost:7781/cmd >/dev/null
kill $RBASRV $RBAP 2>/dev/null || true
wait $RBASRV 2>/dev/null || true
# Bring a second instance up on a copy of that depot with no key of its own: it
# generates a fresh key, sees the foreign head, and re-bases to a clean chain.
RBB=$(mktemp -d)
cp -a "$RBA"/* "$RBB/" 2>/dev/null || true
SMOLMOO_PORT=7782 SMOLMOO_DEPOT="$RBB" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_rbb.log &
RBBSRV=$!
waitgrep /tmp/smolmoo_rbb.log 'world signing on' || true
check_log /tmp/smolmoo_rbb.log 'foreign key' \
	"an instance re-bases a chain signed by a different key"
RBBINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_rbb.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7782/events > /tmp/smolmoo_rbb_p.log &
RBBP=$!
waitgrep /tmp/smolmoo_rbb_p.log "^data: I" || true
RBBSID=$(grep -m1 "^data: I" /tmp/smolmoo_rbb_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$RBBSID create BWiz bpass $RBBINV" \
	http://localhost:7782/cmd >/dev/null
# build a fresh self-signed chain and prove it rewinds to its own genesis
curl -sf -X POST -d "$RBBSID @create #1" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$RBBSID @save" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$RBBSID @create #1" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$RBBSID @save" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$RBBSID @rewind 1" http://localhost:7782/cmd >/dev/null || true
check_log /tmp/smolmoo_rbb.log 'rewound to seq 1' \
	"the re-based chain rewinds to its own genesis"
kill $RBBSRV $RBBP 2>/dev/null || true
rm -rf "$RBA" "$RBA.key" "$RBB" "$RBB.key" \
	/tmp/smolmoo_rba.log /tmp/smolmoo_rba_p.log \
	/tmp/smolmoo_rbb.log /tmp/smolmoo_rbb_p.log

# --- M48b: a damaged history head re-bases, and @history stays coherent ---
# A depot copied without all its objects names a head that will not open. An instance
# re-bases such a head to a fresh chain under its own key rather than dead-ending,
# then @history and @rewind report a clean chain. Build a chain, then point its head
# at an object that is not there. The key is kept, so this is the damaged-depot path,
# not the foreign-key path of M48a.
DCA=$(mktemp -d)
cp -a depot/* "$DCA/" 2>/dev/null || true
SMOLMOO_PORT=7781 SMOLMOO_DEPOT="$DCA" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_dca.log &
DCASRV=$!
waitgrep /tmp/smolmoo_dca.log 'world signing on' || true
DCAINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_dca.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7781/events > /tmp/smolmoo_dca_p.log &
DCAP=$!
waitgrep /tmp/smolmoo_dca_p.log "^data: I" || true
DCASID=$(grep -m1 "^data: I" /tmp/smolmoo_dca_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$DCASID create CWiz cpass $DCAINV" \
	http://localhost:7781/cmd >/dev/null
curl -sf -X POST -d "$DCASID @create #1" http://localhost:7781/cmd >/dev/null
curl -sf -X POST -d "$DCASID @save" http://localhost:7781/cmd >/dev/null
kill $DCASRV $DCAP 2>/dev/null || true
wait $DCASRV 2>/dev/null || true
# Copy the depot and its key (same instance, own topic), then damage the head so it
# names an object that is not present.
DCB=$(mktemp -d)
cp -a "$DCA"/* "$DCB/" 2>/dev/null || true
cp -a "$DCA.key" "$DCB.key" 2>/dev/null || true
printf '%s\n' '0000000000000000000000000000000000000000000000000000000000000000' \
	> "$DCB/head"
SMOLMOO_PORT=7782 SMOLMOO_DEPOT="$DCB" _build/smolmoo serve --bootstrap \
	2>/tmp/smolmoo_dcb.log &
DCBSRV=$!
waitgrep /tmp/smolmoo_dcb.log 'world signing on' || true
check_log /tmp/smolmoo_dcb.log 'head record is missing' \
	"a damaged history head re-bases to a fresh chain"
DCBINV=$(grep -o 'admin invite: [A-Z0-9-]*' /tmp/smolmoo_dcb.log \
	| head -1 | cut -d' ' -f3)
curl -sN http://localhost:7782/events > /tmp/smolmoo_dcb_p.log &
DCBP=$!
waitgrep /tmp/smolmoo_dcb_p.log "^data: I" || true
DCBSID=$(grep -m1 "^data: I" /tmp/smolmoo_dcb_p.log | sed 's/^data: I//')
curl -sf -X POST -d "$DCBSID create DWiz dpass $DCBINV" \
	http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$DCBSID @create #1" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$DCBSID @save" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$DCBSID @create #1" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$DCBSID @save" http://localhost:7782/cmd >/dev/null
curl -sf -X POST -d "$DCBSID @history" http://localhost:7782/cmd >/dev/null
check_log /tmp/smolmoo_dcb_p.log 'World history' \
	"@history lists the re-based chain"
curl -sf -X POST -d "$DCBSID @rewind 1" http://localhost:7782/cmd >/dev/null || true
check_log /tmp/smolmoo_dcb.log 'rewound to seq 1' \
	"@rewind returns to the re-based genesis"
kill $DCBSRV $DCBP 2>/dev/null || true
rm -rf "$DCA" "$DCA.key" "$DCB" "$DCB.key" \
	/tmp/smolmoo_dca.log /tmp/smolmoo_dca_p.log \
	/tmp/smolmoo_dcb.log /tmp/smolmoo_dcb_p.log

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
