#
# Disciple build.
#
# Disciple is the Tabcast e-ink reader: it gets onto Wi-Fi, finds the
# Apostle daemon, and shows the pages it broadcasts. It is an inkerface
# program. inkerface (the sibling repository ../../inkerface) is the
# e-ink platform over the FreeInk SDK: it runs the same program on the
# Xteink X4, on every other reader it supports, and on a desktop, where
# it emulates the X4 pixel for pixel.
#
# This Makefile builds the desktop program and the tests. The reader
# firmware is built by PlatformIO from platformio.ini; `make x4` runs it.
#
# Artifacts do not stay here. This is one project in a multi-project
# repository, and the repository has exactly one build/ and one bin/,
# both at its root. Objects land in build/disciple/<build type>/ and the
# finished program in bin/disciple. Building from inside this directory
# works the same way, because the paths are relative to ROOT_DIR.
#
# Two build types. `debug`, the default, carries full warnings and the
# address and undefined behaviour sanitizers. `release` links the C++
# runtime into the program.
#
#   make                 debug build into <root>/bin/disciple
#   make BUILD=release   release build into <root>/bin/disciple
#   make run             run it in inkerface's preview window
#   make test            build and run the tests
#   make x4              Xteink X4 firmware into <root>/bin/disciple-x4.bin
#   make help            list every target
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

NAME     := disciple
BUILD    ?= debug

# Keep normal builds fully sanitized, with an explicit alternative for
# hosts whose system-wide preload libraries conflict with ASan.
SANITIZERS ?= address,undefined

# Where the repository root is, relative to this project. Everything
# generated goes there and nothing is written beside the sources.
ROOT_DIR ?= ..

# inkerface lives beside tabcast and is built by its own Makefile, which
# runs CMake into its own build/. Disciple links the static libraries
# that build leaves behind.
INKERFACE_DIR ?= $(abspath $(ROOT_DIR)/../inkerface)
INKERFACE_PROFILE := $(if $(filter release,$(BUILD)),release,debug)
INKERFACE_LIBS := $(INKERFACE_DIR)/build/$(INKERFACE_PROFILE)
# inkerface's desktop entry point: main(), which calls program().
INKERFACE_ENTRY := $(INKERFACE_DIR)/lib/inkerface/src/host/entry.cpp

SRC_DIR  := src
LIB_DIR  := lib
INC_DIR  := include
TEST_DIR := tests

BUILD_DIR := $(ROOT_DIR)/build/$(NAME)
BIN_DIR   := $(ROOT_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/$(BUILD)
ifeq ($(BUILD),debug)
    OBJ_DIR := $(OBJ_DIR)/$(SANITIZERS)
endif

CXX ?= g++

# Every translation unit of the program. Sources live in src/ and in
# lib/<name>/, and are found rather than listed, so adding a file to the
# tree is all it takes to get it built.
SOURCES := $(shell find $(SRC_DIR) $(LIB_DIR) -name '*.cpp' 2>/dev/null)
OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

# The entry point, and everything that is not the entry point. Test
# binaries bring their own main(), so they link the second set only.
MAIN_OBJECT  := $(OBJ_DIR)/$(SRC_DIR)/main.o
ENTRY_OBJECT := $(OBJ_DIR)/inkerface/entry.o
LIB_OBJECTS  := $(filter-out $(MAIN_OBJECT),$(OBJECTS))

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINARIES := $(patsubst $(TEST_DIR)/%.cpp,$(OBJ_DIR)/$(TEST_DIR)/%,\
                   $(TEST_SOURCES))

# -MMD -MP writes a .d file beside every .o listing the headers it read,
# which is what makes a header edit trigger a rebuild. inkerface's
# headers come in with -isystem, so this project's warnings judge its own
# code only.
CPPFLAGS += -I$(INC_DIR) -isystem $(INKERFACE_DIR)/include -MMD -MP
CXXFLAGS += -std=c++20 -Wall -Wextra -pedantic

# inkerface's libraries and what they need: X11 for the preview window,
# OpenSSL for TLS. The libraries themselves are linked statically.
LDLIBS += $(INKERFACE_LIBS)/libinkerface_platform.a \
          $(INKERFACE_LIBS)/libfreeink_ui.a \
          $(INKERFACE_LIBS)/libhost_filesystems.a \
          -lssl -lcrypto -lX11

ifeq ($(BUILD),debug)
    CXXFLAGS += -g -Og -fno-omit-frame-pointer \
                -fsanitize=$(SANITIZERS)
    LDFLAGS  += -fsanitize=$(SANITIZERS)
else ifeq ($(BUILD),release)
    CXXFLAGS += -O2 -DNDEBUG
    # Not a full -static link: glibc resolves hostnames through dlopened
    # NSS modules, so a fully static binary would fail to look anything
    # up at runtime.
    LDFLAGS  += -static-libgcc -static-libstdc++
else
    $(error BUILD must be debug or release, got '$(BUILD)')
endif

# The link lands in build/, where every artifact belongs, and the
# finished program is copied to bin/. That keeps bin/ correct when
# switching build types, which a plain timestamp rule would not.
LINKED  := $(OBJ_DIR)/$(NAME)
PROGRAM := $(BIN_DIR)/$(NAME)

# Where `make run` and the VS Code launch keep the emulated reader's
# internal flash, so its Wi-Fi settings and name survive from one run to
# the next. Created by every build.
STORAGE ?= $(abspath $(BUILD_DIR)/storage)

PIO ?= pio
X4_IMAGE := $(BIN_DIR)/disciple-x4.bin

.PHONY: all release run test clean distclean help x4 x4-upload inkerface

## all: build the reader into the repository bin/ (default)
all: $(LINKED)
	@mkdir -p $(BIN_DIR) $(STORAGE)
	@cp -f $< $(PROGRAM)
	@echo "built $(PROGRAM) ($(BUILD), on inkerface)"

## release: shorthand for BUILD=release
release:
	@$(MAKE) --no-print-directory BUILD=release

## inkerface: build inkerface's libraries (done by every build)
# inkerface's own Makefile decides what is out of date, so it is always
# asked. It downloads the FreeInk SDK the first time.
inkerface:
	@$(MAKE) --no-print-directory -C $(INKERFACE_DIR) $(INKERFACE_PROFILE)

$(OBJECTS) $(ENTRY_OBJECT) $(TEST_BINARIES): | inkerface

$(LINKED): $(OBJECTS) $(ENTRY_OBJECT)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(ENTRY_OBJECT) $(LDLIBS)

$(ENTRY_OBJECT): $(INKERFACE_ENTRY)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJECTS) \
	    $(LDLIBS)

## run: build and run the reader in inkerface's preview window
run: all
	@mkdir -p $(STORAGE)
	INKERFACE_PREVIEW=1 INKERFACE_STORAGE_ROOT=$(STORAGE) $(PROGRAM)

## test: build and run every test in tests/
test: $(TEST_BINARIES)
	@for t in $(TEST_BINARIES); do \
	    echo "running $$t"; \
	    $$t || exit 1; \
	done

## x4: build the Xteink X4 firmware into <root>/bin/disciple-x4.bin
# PlatformIO builds it from platformio.ini, on inkerface, into the root
# build/; the image is copied to bin/ like every other finished program.
x4:
	@$(MAKE) --no-print-directory -C $(INKERFACE_DIR) sdk
	$(PIO) run -e x4
	@mkdir -p $(BIN_DIR)
	@cp -f $(ROOT_DIR)/build/disciple/pio/x4/firmware.bin $(X4_IMAGE)
	@echo "built $(X4_IMAGE)"

## x4-upload: build the X4 firmware and flash it over USB
x4-upload: x4
	$(PIO) run -e x4 -t upload

## clean: remove this project's artifacts for the current build type
clean:
	@rm -rf $(BUILD_DIR)/$(BUILD)

# Removes only what this project produced. bin/ is shared with the other
# parts of the repository, so programs are deleted by name rather than
# by emptying the directory.
## distclean: remove all of this project's artifacts and its programs
distclean:
	@rm -rf $(BUILD_DIR) $(PROGRAM) $(X4_IMAGE)

## help: list the targets
help:
	@grep -hE '^## ' $(firstword $(MAKEFILE_LIST)) | sed 's/^## /  /'

# Header dependencies discovered by the compiler. Included last, and with
# a leading dash so a first build with no .d files is not an error.
-include $(OBJECTS:.o=.d)
-include $(ENTRY_OBJECT:.o=.d)
-include $(TEST_BINARIES:=.d)
