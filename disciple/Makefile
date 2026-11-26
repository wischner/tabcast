#
# Disciple build. Run by the top-level Makefile; use that one.
#
# Disciple is the Tabcast e-ink reader, an inkerface program. inkerface
# runs the same program on a reader and, as `emulator`, in a window on a
# desktop, where it emulates the Xteink X4 pixel for pixel.
#
#   PLATFORM=emulator   the desktop program, into <root>/bin/disciple
#   PLATFORM=<reader>   that reader's firmware, built by PlatformIO from
#                       its [env:<reader>] in platformio.ini, into
#                       <root>/bin/disciple-<reader>.bin
#
# Targets: all (the default), upload (flash a reader on PORT), test (the
# tests, on the emulator), clean.
#
# Objects land in <root>/build/disciple/, never beside the sources.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

NAME     := disciple
PLATFORM ?= emulator
BUILD    ?= debug

# Keep debug builds fully sanitized, with an explicit alternative for
# hosts whose system-wide preload libraries conflict with ASan.
SANITIZERS ?= address,undefined

ROOT_DIR ?= ..

# inkerface, downloaded by the top-level Makefile. PlatformIO reads it
# from the environment too (platformio.ini), so it is exported.
INKERFACE_DIR ?= $(abspath $(ROOT_DIR)/deps/inkerface)
export INKERFACE_DIR

ifeq ($(wildcard $(INKERFACE_DIR)/include/inkerface)$(filter clean,$(MAKECMDGOALS)),)
    $(error inkerface is not in $(INKERFACE_DIR); run make at the repository root, which downloads it)
endif

INKERFACE_PROFILE := $(if $(filter release,$(BUILD)),release,debug)
INKERFACE_LIBS := $(INKERFACE_DIR)/build/$(INKERFACE_PROFILE)
# inkerface's desktop entry point: main(), which calls program().
INKERFACE_ENTRY := $(INKERFACE_DIR)/lib/inkerface/src/host/entry.cpp

SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests

BUILD_DIR := $(ROOT_DIR)/build/$(NAME)
BIN_DIR   := $(ROOT_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/$(BUILD)
ifeq ($(BUILD),debug)
    OBJ_DIR := $(OBJ_DIR)/$(SANITIZERS)
endif

CXX ?= g++

SOURCES := $(shell find $(SRC_DIR) -name '*.cpp')
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

PIO      ?= pio
FIRMWARE := $(BIN_DIR)/$(NAME)-$(PLATFORM).bin

.PHONY: all emulator firmware upload test clean inkerface sdk

ifeq ($(PLATFORM),emulator)
all: emulator
else
all: firmware
endif

# Also makes the emulated reader's internal flash, where make run and the
# VS Code launch keep its Wi-Fi settings and name between runs.
emulator: $(LINKED)
	@mkdir -p $(BIN_DIR) $(BUILD_DIR)/storage
	@cp -f $< $(PROGRAM)
	@echo "built $(PROGRAM) ($(BUILD), the emulator)"

# inkerface's libraries, and only those: its demo and its own tests are
# inkerface's business, not a Disciple build's. inkerface's Makefile
# downloads the FreeInk SDK and the filesystem sources the first time;
# its CMake project, configured as that Makefile configures it, builds
# the three libraries into inkerface's own build/. CMake decides what is
# out of date, so it is always asked.
INKERFACE_BUILD_DIR := $(INKERFACE_DIR)/build/$(INKERFACE_PROFILE)
INKERFACE_TARGETS   := inkerface_platform freeink_ui host_filesystems

inkerface:
	@$(MAKE) --no-print-directory -C $(INKERFACE_DIR) sdk filesystems
	@cmake -S $(INKERFACE_DIR) -B $(INKERFACE_BUILD_DIR) \
	    -DCMAKE_BUILD_TYPE=$(if $(filter release,$(BUILD)),Release,Debug) \
	    -DFREEINK_SDK_DIR=$(INKERFACE_DIR)/lib/freeink-sdk \
	    -DINKERFACE_PREVIEW=ON -DINKERFACE_TLS=ON \
	    -DINKERFACE_BIN_DIR=$(INKERFACE_DIR)/bin/$(INKERFACE_PROFILE) \
	    >/dev/null
	@cmake --build $(INKERFACE_BUILD_DIR) --target $(INKERFACE_TARGETS)

sdk:
	@$(MAKE) --no-print-directory -C $(INKERFACE_DIR) sdk

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

# PlatformIO builds into <root>/build/disciple/pio/<platform>/; the image
# is copied to bin/ like every other finished program.
firmware: sdk
	$(PIO) run --environment $(PLATFORM)
	@mkdir -p $(BIN_DIR)
	@cp -f $(BUILD_DIR)/pio/$(PLATFORM)/firmware.bin $(FIRMWARE)
	@echo "built $(FIRMWARE)"

upload: firmware
	$(PIO) run --environment $(PLATFORM) --target upload \
	    $(if $(PORT),--upload-port $(PORT))

test: $(TEST_BINARIES)
	@for t in $(TEST_BINARIES); do \
	    echo "running $$t"; \
	    $$t || exit 1; \
	done

clean:
	@rm -rf $(BUILD_DIR) $(PROGRAM) $(BIN_DIR)/$(NAME)-*.bin

# Header dependencies discovered by the compiler. Included last, and with
# a leading dash so a first build with no .d files is not an error.
-include $(OBJECTS:.o=.d)
-include $(ENTRY_OBJECT:.o=.d)
-include $(TEST_BINARIES:=.d)
