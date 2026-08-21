#
# Tabcast top level build.
#
# Tabcast casts live web browser content onto e-ink tablets. It has
# three parts, each a self-contained project with its own Makefile:
#
#   evangelist   browser extension, sees the page and proclaims it
#   apostle      Linux daemon, carries the message to the flock
#   disciple     e-ink client, displays it for the faithful
#
# This file only orchestrates. Every target here fans out to the same
# target in each part, so `make` builds everything and `make clean`
# cleans everything.
#
#   make                 build all three parts
#   make evangelist      build one part
#   make BUILD=release   release build of the C++ parts
#   make check           syntax check and test everything
#   make help            list every target
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

# The C++ parts, which share a build type and a set of targets.
NATIVE := apostle disciple

# The browser extension, built by python3 rather than a compiler.
WEB := evangelist

PARTS := $(WEB) $(NATIVE)

# Passed down to the C++ parts. debug, the default, has sanitizers.
# release links the C++ runtime in and ships one file per program.
BUILD ?= debug

.PHONY: all $(PARTS) release dist check test clean distclean help

## all: build all three parts (default)
all: $(PARTS)

## evangelist: build the browser extension for Firefox and Chrome
## apostle: build the broadcast daemon
## disciple: build the e-ink client
$(WEB):
	@$(MAKE) --no-print-directory -C $@

$(NATIVE):
	@$(MAKE) --no-print-directory -C $@ BUILD=$(BUILD)

## release: release build of everything, shorthand for BUILD=release
release:
	@$(MAKE) --no-print-directory BUILD=release

## dist: package the extension for side-loading, build the rest
dist: $(NATIVE)
	@$(MAKE) --no-print-directory -C $(WEB) dist

## check: syntax check the extension and run the C++ tests
check:
	@$(MAKE) --no-print-directory -C $(WEB) check
	@for part in $(NATIVE); do \
	    $(MAKE) --no-print-directory -C $$part BUILD=$(BUILD) test \
	        || exit 1; \
	done

## test: alias for check
test: check

## clean: remove build artifacts from every part
clean:
	@for part in $(PARTS); do \
	    $(MAKE) --no-print-directory -C $$part clean || exit 1; \
	done

## distclean: remove every build artifact and every built program
distclean:
	@$(MAKE) --no-print-directory -C $(WEB) clean
	@for part in $(NATIVE); do \
	    $(MAKE) --no-print-directory -C $$part distclean || exit 1; \
	done

## help: list the targets
help:
	@grep -hE '^## ' $(firstword $(MAKEFILE_LIST)) | sed 's/^## /  /'
