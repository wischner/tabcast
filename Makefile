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
# There is exactly one build/ and one bin/, both here at the root. No
# part writes artifacts beside its own sources:
#
#   build/apostle/<type>/    objects, deps, intermediate links
#   build/disciple/<type>/
#   build/evangelist/<browser>/   unpacked extension, load this
#   bin/apostle                   finished programs
#   bin/disciple
#   bin/evangelist-<version>-firefox.xpi
#   bin/evangelist-<version>-chrome.zip
#
#   make                 build all three parts
#   make evangelist      build one part
#   make BUILD=release   release build of the C++ parts
#   make deb             release build and Debian package
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

# Debian packaging always makes a release build. The test backend is
# the distributable desktop Disciple; override this only when testing
# the package machinery on a machine without gtkmm.
PACKAGE_VERSION ?= $(shell python3 -c \
    'import json; print(json.load(open("evangelist/src/manifest.json"))["version"])')
DISCIPLE_BACKEND ?= test

.PHONY: all $(PARTS) release dist deb check test clean distclean help

## all: build all three parts (default)
all: $(PARTS)

## evangelist: build and package the extension for Firefox and Chrome
## apostle: build the broadcast daemon
## disciple: build the e-ink client
$(WEB):
	@$(MAKE) --no-print-directory -C $@

$(NATIVE):
	@$(MAKE) --no-print-directory -C $@ BUILD=$(BUILD)

## release: release build of everything, shorthand for BUILD=release
release:
	@$(MAKE) --no-print-directory BUILD=release

## dist: build all distributable outputs (currently the same as all)
dist: $(NATIVE)
	@$(MAKE) --no-print-directory -C $(WEB) dist

## deb: build one Debian package containing all three parts
deb:
	@$(MAKE) --no-print-directory -C apostle BUILD=release
	@$(MAKE) --no-print-directory -C disciple BUILD=release \
	    BACKEND=$(DISCIPLE_BACKEND)
	@$(MAKE) --no-print-directory -C evangelist dist
	@packaging/build-deb.sh "$(PACKAGE_VERSION)"

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
	@for part in $(PARTS); do \
	    $(MAKE) --no-print-directory -C $$part distclean || exit 1; \
	done
	@rm -f dist/tabcast_*.deb
	@rmdir build bin 2>/dev/null || true
	@rmdir dist 2>/dev/null || true

## help: list the targets
help:
	@grep -hE '^## ' $(firstword $(MAKEFILE_LIST)) | sed 's/^## /  /'
