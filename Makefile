#
# Tabcast.
#
# Tabcast casts live web browser content onto e-ink readers:
#
#   evangelist   browser extension, sees the page and proclaims it
#   apostle      Linux daemon, carries the message to the flock
#   disciple     e-ink reader, displays it for the faithful
#
# This is the only Makefile you need. `make help` lists what it does.
#
#   make                     build all three into bin/
#   make run                 run them all on this machine
#   make deploy PLATFORM=x4  build Disciple for a reader and flash it
#   make clean               delete everything a build made
#   make purge               clean, and delete what builds downloaded
#
# Builds run in Docker, in the image docker/Dockerfile describes, so a
# machine needs only make, git, curl, and Docker. The image is built the
# first time.
# DOCKER=no builds with this machine's own tools instead; that is also
# what runs inside the image.
#
# Third-party libraries are downloaded into deps/ the first time and
# kept: inkerface (Disciple's platform, which brings the FreeInk SDK),
# stb_image (Apostle's PNG and JPEG decoder), and PlatformIO's reader
# toolchains. Each is pinned below. deps/ is not checked in, survives
# `make clean`, and goes with `make purge`.
#
# Output, the same with or without Docker:
#
#   bin/apostle                        the daemon
#   bin/disciple                       the reader, emulated on a desktop
#   bin/disciple-<platform>.bin        the reader's firmware
#   bin/evangelist-<version>-*.xpi|zip the extension
#   build/                             everything intermediate
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

# --- What to build -----------------------------------------------------

# Which Disciple to build. `emulator` is the program that emulates a
# reader in a window on this machine. Every other platform is an
# [env:...] of disciple/platformio.ini, so adding a reader there adds it
# here and to `make help`.
PLATFORM ?= emulator
READERS   := $(shell sed -n 's/^\[env:\(.*\)\]$$/\1/p' disciple/platformio.ini)
PLATFORMS := emulator $(READERS)

ifeq ($(filter $(PLATFORM),$(PLATFORMS)),)
    $(error unknown PLATFORM '$(PLATFORM)'; one of: $(PLATFORMS))
endif
ifneq ($(filter deploy,$(MAKECMDGOALS)),)
    ifeq ($(PLATFORM),emulator)
        $(error deploy needs a reader: make deploy PLATFORM=<platform>, one of: $(READERS))
    endif
endif

# release is what bin/ is for. debug adds symbols and sanitizers, for the
# debugger.
BUILD ?= release

DOCKER ?= yes

# --- Third-party libraries, pinned -------------------------------------

DEPS := $(CURDIR)/deps

INKERFACE_URL := https://github.com/wischner/inkerface.git
INKERFACE_REV := 1321cc6751cf09880f17fcf6f015923cfb6ec21d
# Point this at a checkout of your own to build against it instead. It
# is then never downloaded, moved, or cleaned.
# Absolute, because the Disciple build runs from disciple/ and Docker
# mounts it by path.
INKERFACE_DIR ?= $(DEPS)/inkerface
override INKERFACE_DIR := $(abspath $(INKERFACE_DIR))
MANAGED_INKERFACE := $(DEPS)/inkerface

STB_REV    := f58f558c120e9b32c217290b80bad1a0729fbb2c
STB_SHA256 := 594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
STB_URL    := https://raw.githubusercontent.com/nothings/stb/$(STB_REV)/stb_image.h
STB_DIR    := $(DEPS)/stb

# PlatformIO keeps its toolchains here rather than in ~/.platformio.
export PLATFORMIO_CORE_DIR := $(DEPS)/platformio

# --- Docker ------------------------------------------------------------

# Named after the Dockerfile's contents, so editing it builds a new one.
IMAGE := tabcast-build:$(shell sha256sum docker/Dockerfile | cut -c1-12)

# The repository at the same path inside as outside, as this user, on
# this machine's network, which discovery and downloads both want.
docker_run = docker run --rm --init \
    --user $(shell id -u):$(shell id -g) \
    --network host \
    --volume $(CURDIR):$(CURDIR) \
    $(if $(filter $(CURDIR)/%,$(INKERFACE_DIR)),,\
        --volume $(INKERFACE_DIR):$(INKERFACE_DIR)) \
    --workdir $(CURDIR) \
    $(1) $(IMAGE)

# What the build inside Docker is told.
passed := PLATFORM=$(PLATFORM) BUILD=$(BUILD) INKERFACE_DIR=$(INKERFACE_DIR) \
    $(if $(SANITIZERS),SANITIZERS=$(SANITIZERS))

# --- Deploying ---------------------------------------------------------

# The first reader on USB, unless PORT says otherwise.
PORT ?= $(firstword $(wildcard /dev/ttyACM* /dev/ttyUSB*))

JOBS := $(shell nproc 2>/dev/null || echo 4)

.PHONY: all run deploy clean purge help image deps evangelist apostle \
        disciple upload

# --- Commands ----------------------------------------------------------

ifeq ($(DOCKER),yes)

# Libraries are downloaded out here, with this machine's git and its
# credentials, so a private repository downloads for whoever can see it.
all: image deps
	@$(call docker_run,) make --no-print-directory DOCKER=no $(passed)

deploy: image deps
	@if [ -z "$(PORT)" ]; then \
	    echo "deploy     no reader on USB. Connect it, switch it on, and" \
	         "try again, or name its port: PORT=/dev/ttyACM0" >&2; \
	    exit 1; \
	fi
	@$(call docker_run,--device $(PORT) --group-add $(shell stat -c %g $(PORT) 2>/dev/null)) \
	    make --no-print-directory DOCKER=no $(passed) PORT=$(PORT) upload

image:
	@if ! command -v docker >/dev/null 2>&1; then \
	    echo "Tabcast builds in Docker, and docker is not installed." >&2; \
	    echo "Install Docker, or build with this machine's own tools:" \
	         "make DOCKER=no" >&2; \
	    exit 1; \
	fi
	@if ! docker image inspect $(IMAGE) >/dev/null 2>&1; then \
	    echo "docker     building the build image $(IMAGE), once"; \
	    docker build --tag $(IMAGE) docker; \
	fi

else

all: evangelist apostle disciple

deploy: upload

endif

# Build, then run Apostle, Evangelist in Firefox, and Disciple. On the
# emulator platform the reader is a window here. On any other, it is the
# reader itself, deployed beforehand, which finds this Apostle.
run: all
	@scripts/run.sh $(PLATFORM)

clean:
	@rm -rf build bin dist
	@rm -rf $(MANAGED_INKERFACE)/build $(MANAGED_INKERFACE)/bin \
	    $(MANAGED_INKERFACE)/.pio
	@find apostle disciple evangelist scripts packaging \
	    -name __pycache__ -prune -exec rm -rf {} +
	@echo "cleaned build/, bin/, and dist/; deps/ kept"

purge: clean
	@rm -rf deps
	@if command -v docker >/dev/null 2>&1; then \
	    docker image ls --quiet tabcast-build | sort -u \
	        | xargs --no-run-if-empty docker image rm --force >/dev/null; \
	fi
	@echo "purged deps/ and the build image"

help:
	@echo "Tabcast"
	@echo
	@echo "  make                       build Evangelist, Apostle, and Disciple into bin/"
	@echo "  make run                   build, then run all three on this machine"
	@echo "  make deploy PLATFORM=...   build Disciple for a reader and flash it over USB"
	@echo "  make clean                 delete everything builds made; keep deps/"
	@echo "  make purge                 clean, and delete deps/ and the build image"
	@echo "  make help                  this text"
	@echo
	@echo "  PLATFORM=<platform>        which Disciple to build, run, or deploy"
	@echo
	@echo "  emulator    a reader in a window on this machine, bin/disciple (default)"
	@awk '/^\[env:/ { name = substr($$0, 6, length($$0) - 6) } \
	     /^custom_description *=/ && name != "" { \
	         sub(/^custom_description *= */, ""); \
	         printf "  %-11s %s, bin/disciple-%s.bin\n", name, $$0, name; \
	         name = "" }' disciple/platformio.ini
	@echo
	@echo "Builds run in Docker; this machine needs only make, git, curl, and Docker."

# --- The build itself, with this machine's tools or inside Docker ------

deps:
ifeq ($(INKERFACE_DIR),$(MANAGED_INKERFACE))
	@scripts/deps.sh git $(MANAGED_INKERFACE) $(INKERFACE_URL) $(INKERFACE_REV)
endif
	@scripts/deps.sh file $(STB_DIR)/stb_image.h $(STB_URL) $(STB_SHA256)

evangelist:
	@$(MAKE) --no-print-directory -C evangelist

apostle: deps
	@$(MAKE) --no-print-directory -C apostle -j$(JOBS) BUILD=$(BUILD) \
	    STB_DIR=$(STB_DIR) $(if $(SANITIZERS),SANITIZERS=$(SANITIZERS))

disciple: deps
	@$(MAKE) --no-print-directory -C disciple -j$(JOBS) $(passed)

upload: deps
	@$(MAKE) --no-print-directory -C disciple $(passed) PORT=$(PORT) upload
