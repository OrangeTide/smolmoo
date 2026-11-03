# Makefile -- build, preview, and check a Markdown site for GitHub/GitLab Pages.
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
#
#   make            Build the site into _site/
#   make serve      Build, then serve at http://127.0.0.1:8000/
#   make watch      Serve and rebuild whenever content/ or site/ changes
#   make check      Preflight the output: dead links, absolute paths, Jekyll
#   make diagrams   Render .dot/.msc/.plt/.pic/.d2/.mmd sources to SVG
#   make drafts     Build drafts too, into _site-drafts/ (never into _site/)
#   make serve-drafts  Build drafts and preview them
#   make feed       Regenerate feed.xml (needs SITE_URL)
#   make clean      Remove _site/ (clean-diagrams removes rendered figures)
#
# Requires: lowdown or pandoc, plus a POSIX shell and make. Everything else is
# optional: darkhttpd/caddy/busybox for preview, inotify-tools for watch.

SITE_TITLE   ?= smolmoo
SITE_TAGLINE ?= A small web MUD/MOO server, and the ChromeSix SRD
SITE_URL     ?= https://orangetide.github.io/smolmoo
INDEX_MODE   ?= list
PORT         ?= 8000

CONTENT   := content
OUT       ?= _site
DRAFT_OUT ?= _site-drafts
TEMPLATES := site/templates
STATIC    := site/static
BIN       := site/bin

export SITE_TITLE SITE_TAGLINE SITE_URL

SOURCES := $(shell find $(CONTENT) -name '*.md' 2>/dev/null)
ASSETS  := $(shell find $(TEMPLATES) $(STATIC) -type f 2>/dev/null)

# Diagram and plot sources render to an SVG beside themselves, which the page
# builder then copies into the output like any other companion file. Each
# source is its own target, so make rebuilds only the figures that changed.
DIAGRAM_SRC := $(shell find $(CONTENT) \( -name '*.dot' -o -name '*.gv' \
	-o -name '*.msc' -o -name '*.plt' -o -name '*.gnuplot' -o -name '*.pic' \
	-o -name '*.d2' -o -name '*.mmd' \) 2>/dev/null)
DIAGRAMS := $(addsuffix .svg,$(basename $(DIAGRAM_SRC)))

# Serialize regardless of -j. The recipes share state that parallel execution
# would corrupt: build-site.sh writes pages.json, which gen-index.sh and
# gen-feed.sh then read, and figure rendering writes into the same content tree
# the page builder walks. A parallel run does not fail loudly; it produces an
# index missing entries, which looks like a content bug days later.
#
# .NOTPARALLEL applies to the whole file: GNU make before 4.4 ignores any
# prerequisites given here, so a targeted form would silently do nothing on
# older systems. The lost speedup is small next to a wrong site.
.NOTPARALLEL:

.PHONY: all site serve watch check drafts serve-drafts diagrams clean-diagrams feed clean help

all: site

site: $(OUT)/index.html

diagrams: $(DIAGRAMS)

# One rule per source language; build-diagrams.sh knows the tool invocations so
# the Makefile only has to know what depends on what.
%.svg: %.dot ; $(BIN)/build-diagrams.sh $<
%.svg: %.gv ; $(BIN)/build-diagrams.sh $<
%.svg: %.msc ; $(BIN)/build-diagrams.sh $<
%.svg: %.plt ; $(BIN)/build-diagrams.sh $<
%.svg: %.gnuplot ; $(BIN)/build-diagrams.sh $<
%.svg: %.pic ; $(BIN)/build-diagrams.sh $<
%.svg: %.d2 ; $(BIN)/build-diagrams.sh $<
%.svg: %.mmd ; $(BIN)/build-diagrams.sh $<

# Generated SVGs are prerequisites of the site, not of a phony target, so a
# figure whose source changed forces exactly one re-render and one rebuild.
# One stamp target for the whole site: the builder walks the tree itself, so
# per-page rules would only duplicate that traversal.
$(OUT)/index.html: $(SOURCES) $(DIAGRAMS) $(ASSETS) $(BIN)/build-site.sh $(BIN)/gen-index.sh
	$(BIN)/build-site.sh -o $(OUT) -t $(TEMPLATES) -s $(STATIC) $(CONTENT)
	$(BIN)/gen-index.sh -o $(OUT) -t $(TEMPLATES) -m $(INDEX_MODE)
	$(BIN)/gen-feed.sh -o $(OUT)

# Drafts build into their own directory. Writing them into $(OUT) would leave
# unpublished pages sitting in the directory CI uploads, and the newer
# timestamp would make the next `make site` a no-op, so the drafts would
# deploy. Separate directories make that impossible rather than unlikely.
drafts:
	$(BIN)/build-site.sh -a -o $(DRAFT_OUT) -t $(TEMPLATES) -s $(STATIC) $(CONTENT)
	$(BIN)/gen-index.sh -o $(DRAFT_OUT) -t $(TEMPLATES) -m $(INDEX_MODE)
	@echo "Drafts built in $(DRAFT_OUT). Preview with: make serve-drafts"

serve-drafts: drafts
	$(BIN)/serve.sh -p $(PORT) $(DRAFT_OUT)

feed:
	$(BIN)/gen-feed.sh -o $(OUT)

check: site
	$(BIN)/check-site.sh $(OUT)

serve: site
	$(BIN)/serve.sh -p $(PORT) $(OUT)

watch: site
	$(BIN)/serve.sh -p $(PORT) -w $(CONTENT) -w site -c "$(MAKE) site" $(OUT)

# Generated figures live beside their sources so they render on GitHub too.
# They are removed only on request: a contributor without graphviz installed
# should still be able to build the prose.
clean-diagrams:
	rm -f $(DIAGRAMS)

clean:
	rm -rf $(OUT) $(DRAFT_OUT)

help:
	@sed -n '4,12p' $(firstword $(MAKEFILE_LIST))
