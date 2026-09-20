<!-- SPDX-License-Identifier: 0BSD OR CC0-1.0 -->
# Future Features

Ideas that fall out naturally from what smolmoo already has: a
content-addressed store (CAS) for the world, BLAKE2b addressing, a
vendored monocypher, and the signing and tree layers available in
upstream smolvfs. None of these are committed work. They are a menu of
directions the current foundation makes cheap to reach.

Each entry notes the smolvfs pieces it leans on, so the build cost is
visible up front.

Online creation (letting builders make areas, rooms, items, NPCs, stores,
and vehicles in-game) shipped as its own arc on the verb VM rather than the
CAS and signing foundation. Its design record is `OLC.md` and the summary is
Milestone 32 in `PLAN.md`.

## Versioned world history (shipped: Milestones 29-31)

Done, and the foundation several items below build on. Each world save
appends a signed version record, chained to its predecessor, forming an
append-only, verifiable log of world roots. `@history` lists it, `@rewind`
restores an earlier root, `@gc` bounds the depot growth it creates, and
`@fsck` verifies the store and the signed chain.

Leans on: `cas-sign` (records and chain walk), monocypher (already
vendored). No `cas-tree` needed for the local case.

## SHOAL: signed topic publish and subscribe

Publish the world root as a signed topic, a mutable name that always
tracks the newest signed record. A subscriber verifies the topic
against the publisher's key alone, with no registry and no trust in
whoever served the bytes. The record verifies the same over HTTP, from
a peer, or off a USB stick.

Leans on: `cas-sign` plus `cas-topic` and `cas-tree` (the ref machinery
that keeps a topic head with an update log and crash rollback).

Deferred with the federation stack (see "Multi-server peering and
federation").

## Multi-server peering and federation

Servers address every object by hash, so two smolmoo instances can
exchange objects without a shared database. A shared object space opens
cross-server portals, shared prototypes, or a hub-and-spoke of themed
worlds that pull common content from one another. Because addresses are
hashes, an object pulled from a peer is either bit-for-bit what its
address claims or it is rejected.

Leans on: CAS addressing, a fetch-by-hash sync protocol (REEF, the
have/want exchange upstream describes), `cas-sign` for trust between
peers.

Deferred for now, by decision. This is the anchor of the federation
stack (SHOAL above, the backup follower below, and REEF sync), and none
of it is cheap: the have/want sync protocol is not vendored, `cas-topic`
and `cas-tree` are not vendored, and a networked object exchange is a new
subsystem rather than a small addition. The local half of the story has
already shipped as versioned history (`@history`/`@rewind`/`@gc`/`@fsck`),
which is what most single-server worlds need. Revisit when a concrete
multi-server or off-site use case justifies vendoring the sync and ref
layers.

## Remote backup follower

A read-only follower subscribes to a server's world topic, pulls each
new signed root, verifies it, and keeps an off-site replica. Disaster
recovery without trusting the network path: the follower can prove the
backup is exactly what the origin published. Promote a follower to
primary if the origin is lost.

Leans on: `cas-sign`, `cas-topic`, the sync protocol.

Deferred with the federation stack (see "Multi-server peering and
federation").

## World branching and seasonal forks

Content-addressed roots make a branch nearly free: point a second head
at the current root and diverge. Run a test world, a seasonal event, or
a builder sandbox as a fork, then keep it or discard it without
touching the live world. Roots that share history share storage.

Leans on: the version chain, optionally `cas-tree` for named branches.

## Content-addressed asset store

Player uploads (images, maps, handouts, character portraits) stored in
CAS are deduplicated automatically and served by hash. Identical files
cost one copy no matter how many players attach them. Versioned trees
of player content snapshot the same way the world does.

Leans on: CAS, `cas-tree` and `vfs-snap` for content trees.

## Signed content and verb packs

Publish themed content packs as signed CAS objects. A server fetches a
pack by address and verifies its signature before installing it, so a
world can pull a "mod" from a publisher it trusts without a trusted
download channel.

A pack could carry precompiled verb ELFs, or verb source. In-game
programming already compiles both MooScript and C through `@program`, so
a source pack would install by running that same pipeline on the
receiving server, which keeps the shipped artifact readable and lets
each server compile against its own toolchain. Precompiled ELFs suit
serve-only deployments that do not ship the SDK.

Leans on: `cas-sign`, the sync protocol, the existing `@program` compile
pipeline.

## Offline and air-gapped world transfer

Because a signed record verifies off any medium, a whole world moves
between machines on a USB stick or a single file, with integrity intact
and no trusted channel required. Useful for demos, migrations, and
restoring a world onto new hardware.

Leans on: `cas-sign`, a snapshot export.

## Property and object history

Objects are already versioned by the world chain, so per-property
history, diff, and blame come almost for free: walk the chain, load the
object at each root, and compare. Useful for moderation, for undoing a
single bad property edit, and for a builder's "what did I change" view.

Leans on: the version chain, object load at an arbitrary root.

## Depot compaction (packing)

Reclaim and integrity have shipped. `@gc` (Milestone 30) garbage-collects
objects no retained root reaches and prunes old history, and `@fsck` (Milestone
31) verifies the depot against its own hashes, checks live-world reachability,
and walks the signed history chain. What remains is packing: fold the many small
loose objects into a packfile to cut per-file overhead, which matters most on the
SD-card target where inode and directory-entry costs add up.

Deferred for now, by decision, as low payoff against the code it would take:

- The vendored `cas-pack` is bundle-oriented. `cas_pack_create` builds a
  `pack.dat` from loose objects but does not delete them or merge an existing
  pack, and the store has no runtime pack-rebuild API. Real space reclamation
  (delete loose after packing) that stays correct across repeated packing needs
  either an offline subcommand that rebuilds the whole store through a scratch
  copy, or new vendored `cas_detach_pack` / `cas_reopen_pack` calls for a live
  command. Both are a fair amount of tricky file-swapping code.
- No compressor is vendored (the miniz codec is left out), so packing would
  save only per-file overhead, not bytes.
- `@gc` already bounds the loose file count by removing garbage, so growth is
  not unbounded.

Revisit if the depot file count becomes a real problem on the target, or if
upstream grows an in-place repack API. Bringing in the miniz codec would add
compression on top.

Leans on: `cas-pack` (already vendored), plus an offline rebuild or new
vendored repack calls; optionally the miniz codec for compression.
