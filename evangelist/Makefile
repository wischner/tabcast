#
# Evangelist build.
#
# Produces one unpacked, loadable extension tree per browser, and
# optionally an archive of each. The only dependency is python3, which
# Ubuntu and Debian already ship, so a clean checkout builds with no
# network and no package manager.
#
# Artifacts do not stay here. This is one project in a multi-project
# repository, and the repository has exactly one build/ and one bin/,
# both at its root. The unpacked trees go to build/evangelist/<browser>
# because they are what a developer loads while working, and the
# archives go to bin/ because they are what you hand to someone else.
#
#   make                 unpacked trees and archives into <root>/bin/
#   make dist            same as make, for distribution workflows
#   make check           syntax check the manifests and the scripts
#   make help            list every target
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

NAME := evangelist

# Where the repository root is, relative to this project. Everything
# generated goes there and nothing is written beside the sources.
ROOT_DIR ?= ..

SRC     := src
TOOLS   := tools

BUILD_DIR := $(ROOT_DIR)/build/$(NAME)
BIN_DIR   := $(ROOT_DIR)/bin

PYTHON ?= python3

VERSION := $(shell $(PYTHON) -c \
    'import json; print(json.load(open("$(SRC)/manifest.json"))["version"])')

ICON_SIZES := 16 32 48 128
ICONS      := $(foreach s,$(ICON_SIZES),$(SRC)/icons/icon-$(s).png)

SOURCES := $(shell find $(SRC) -type f 2>/dev/null)
SCRIPTS := $(shell find $(SRC) -name '*.js' 2>/dev/null)
JSON    := $(wildcard $(SRC)/manifest*.json)

# Keep the exact current outputs for the default target, and patterns
# for cleaning archives left by earlier versions.
FIREFOX_ARCHIVE := $(BIN_DIR)/$(NAME)-$(VERSION)-firefox.xpi
CHROME_ARCHIVE  := $(BIN_DIR)/$(NAME)-$(VERSION)-chrome.zip
ARCHIVES        := $(FIREFOX_ARCHIVE) $(CHROME_ARCHIVE)
ARCHIVE_PATTERNS := $(BIN_DIR)/$(NAME)-*.xpi \
                    $(BIN_DIR)/$(NAME)-*.zip

.PHONY: all firefox chrome dist icons check clean distclean help

# `all` is phony and carries a recipe of its own, so it always reports
# where the build landed, exactly like the C++ parts do. Hanging it
# straight off the stamp instead would make an up-to-date tree print
# make's own "Nothing to be done for 'all'", which reads like the
# extension was skipped rather than already built.
## all: build and package every browser target (default)
all: $(BUILD_DIR)/.stamp
	@if [ ! -f "$(FIREFOX_ARCHIVE)" ] \
	        || [ ! -f "$(CHROME_ARCHIVE)" ]; then \
	    $(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD_DIR) \
	        --dist $(BIN_DIR) >/dev/null \
	        && touch $(BUILD_DIR)/.stamp; \
	fi
	@echo "built $(BUILD_DIR)/firefox and $(BUILD_DIR)/chrome"
	@echo "packaged $(FIREFOX_ARCHIVE) and $(CHROME_ARCHIVE)"

# The stamp is what actually tracks the work. It is newer than every
# source once a build succeeds, so the tree is only rebuilt when
# something really changed.
$(BUILD_DIR)/.stamp: $(SOURCES) $(ICONS) $(TOOLS)/build.py Makefile
	@mkdir -p $(BUILD_DIR)
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD_DIR) \
	    --dist $(BIN_DIR) >/dev/null
	@touch $@

## firefox: build only the Firefox target
firefox: $(ICONS)
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD_DIR) \
	    --target firefox
	@rm -f $(BUILD_DIR)/.stamp

## chrome: build only the Chrome target
chrome: $(ICONS)
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD_DIR) \
	    --target chrome
	@rm -f $(BUILD_DIR)/.stamp

## dist: alias for the packaged default build
dist: all

## icons: regenerate the icon set from tools/make_icons.py
icons: $(ICONS)

$(ICONS): $(TOOLS)/make_icons.py
	@$(PYTHON) $(TOOLS)/make_icons.py $(SRC)/icons

## check: syntax check every source file
check:
	@for f in $(JSON); do \
	    $(PYTHON) -c "import json,sys; json.load(open(sys.argv[1]))" $$f \
	        || exit 1; \
	done
	@echo "manifests parse"
	@if command -v node >/dev/null 2>&1; then \
	    for f in $(SCRIPTS); do \
	        node --input-type=module --check < $$f || exit 1; \
	    done; \
	    echo "scripts parse"; \
	else \
	    echo "node not installed, skipping the JavaScript syntax check"; \
	fi

# Removes only what this project produced. bin/ is shared with the
# other parts of the repository, so the archives are deleted by name
# rather than by emptying the directory.
## clean: remove this project's build output and archives
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(ARCHIVE_PATTERNS)

## distclean: same as clean, this project has no other artifacts
distclean: clean

## help: list the targets
help:
	@grep -hE '^## ' $(firstword $(MAKEFILE_LIST)) | sed 's/^## /  /'
