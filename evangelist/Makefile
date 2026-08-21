#
# Evangelist build.
#
# Produces one unpacked, loadable extension tree per browser under
# build/, and optionally an archive of each. The only dependency is
# python3, which Ubuntu and Debian already ship, so a clean checkout
# builds with no network and no package manager.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

PYTHON  ?= python3
SRC     := src
BUILD   := build
TOOLS   := tools

ICON_SIZES := 16 32 48 128
ICONS      := $(foreach s,$(ICON_SIZES),$(SRC)/icons/icon-$(s).png)

SOURCES := $(shell find $(SRC) -type f 2>/dev/null)
SCRIPTS := $(shell find $(SRC) -name '*.js' 2>/dev/null)
JSON    := $(wildcard $(SRC)/manifest*.json)

.PHONY: all firefox chrome dist icons check clean help

## all: build every browser target into build/
all: $(BUILD)/.stamp

$(BUILD)/.stamp: $(SOURCES) $(ICONS) $(TOOLS)/build.py
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD)
	@touch $@

## firefox: build only the Firefox target
firefox: $(ICONS)
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD) --target firefox

## chrome: build only the Chrome target
chrome: $(ICONS)
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD) --target chrome

## dist: build every target and package it for side-loading
dist: $(ICONS)
	@$(PYTHON) $(TOOLS)/build.py --src $(SRC) --out $(BUILD) --zip

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
	    for f in $(SCRIPTS); do node --check $$f || exit 1; done; \
	    echo "scripts parse"; \
	else \
	    echo "node not installed, skipping the JavaScript syntax check"; \
	fi

## clean: remove all build output
clean:
	@rm -rf $(BUILD)

## help: list the targets
help:
	@grep -hE '^## ' $(firstword $(MAKEFILE_LIST)) | sed 's/^## /  /'
