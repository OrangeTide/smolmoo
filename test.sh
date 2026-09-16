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

# permission denial: create non-admin player
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

# non-admin tries to write existing property — should be denied
curl -sf -X POST -d "$SID3 testsetprop Rusty Sword" http://localhost:$PORT/cmd >/dev/null
check_log /tmp/smolmoo_p3.log 'SETPROP:DENIED' "sys_setprop denied"

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

echo "---"
echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
