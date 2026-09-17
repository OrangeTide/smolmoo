<!-- SPDX-License-Identifier: 0BSD OR CC0-1.0 -->
# Future Features

Ideas that fall out naturally from what smolmoo already has: a
content-addressed store (CAS) for the world, BLAKE2b addressing, a
vendored monocypher, and the signing and tree layers available in
upstream smolvfs. None of these are committed work. They are a menu of
directions the current foundation makes cheap to reach.

Each entry notes the smolvfs pieces it leans on, so the build cost is
visible up front.

## Versioned world history (shipped: Milestones 29-30)

Done, and the foundation several items below build on. Each world save
appends a signed version record, chained to its predecessor, forming an
append-only, verifiable log of world roots. `@history` lists it, `@rewind`
restores an earlier root, and `@gc` bounds the depot growth it creates.

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

## Remote backup follower

A read-only follower subscribes to a server's world topic, pulls each
new signed root, verifies it, and keeps an off-site replica. Disaster
recovery without trusting the network path: the follower can prove the
backup is exactly what the origin published. Promote a follower to
primary if the origin is lost.

Leans on: `cas-sign`, `cas-topic`, the sync protocol.

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

Publish compiled verb ELFs or themed content packs as signed CAS
objects. A server fetches a pack by address and verifies its signature
before installing it, so a world can pull a "mod" from a publisher it
trusts without a trusted download channel.

Leans on: `cas-sign`, the sync protocol.

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

## Depot integrity and compaction

The reclaim half shipped as Milestone 30: `@gc` garbage-collects objects no
retained root reaches and prunes old history, bounding growth for a
mutation-heavy world. Two pieces remain. First, integrity: verify the depot
against its own hashes to detect corruption early (an `@fsck`-style pass over
reachable objects). Second, packing: fold the many small loose objects into
packfiles to cut per-file overhead.

Leans on: `cas-tree` fsck (integrity), `cas-pack` (packing, already vendored).
