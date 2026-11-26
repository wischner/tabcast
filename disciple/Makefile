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
# Both link inkerface's latest release, which the top-level Makefile
# keeps in <root>/deps/inkerface: the Linux libraries through their
# pkg-config file, and the reader's library through its platformio.ini.
# inkerface itself is never built here.
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

# inkerface's release. PlatformIO reads it from the environment too
# (platformio.ini), so it is exported.
INKERFACE_DIR ?= $(abspath $(ROOT_DIR)/deps/inkerface)
export INKERFACE_DIR

ifeq ($(wildcard $(INKERFACE_DIR)/VERSION)$(filter clean,$(MAKECMDGOALS)),)
    $(error inkerface is not in $(INKERFACE_DIR); run make at the repository root, which downloads it)
endif

# The Linux library for this build type: include/, and lib/ with a
# relocatable pkg-config file, which knows what the library needs (X11,
# OpenSSL, threads).
INKERFACE_PREFIX := $(INKERFACE_DIR)/linux-$(if $(filter release,$(BUILD)),release,debug)
inkerface_config  = $(shell PKG_CONFIG_PATH=$(INKERFACE_PREFIX)/lib/pkgconfig \
                        pkg-config $(1) inkerface)

SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests

BUILD_DIR := $(ROOT_DIR)/build/$(NAME)
BIN_DIR   := $(ROOT_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/$(BUILD)
ifeq ($(BUILD),debug)
    OBJ_DIR := $(OBJ_DIR)/$(SANITIZERS)
endif

# The version, when a release stamps one in; otherwise version.h's own.
# A stamp file holds the last one built, so changing it rebuilds.
VERSION ?=
VERSION_STAMP := $(OBJ_DIR)/version
ifeq ($(filter clean,$(MAKECMDGOALS)),)
    $(shell mkdir -p $(OBJ_DIR) && { [ -f $(VERSION_STAMP) ] && \
        [ "$$(cat $(VERSION_STAMP))" = "$(VERSION)" ]; } || \
        echo '$(VERSION)' > $(VERSION_STAMP))
endif
CPPFLAGS += $(if $(VERSION),-DTABCAST_VERSION='"$(VERSION)"')

# The same, for the firmware, which PlatformIO compiles
# (build_src_flags in platformio.ini).
export TABCAST_VERSION_FLAG := $(if $(VERSION),'-DTABCAST_VERSION="$(VERSION)"')

CXX ?= g++

SOURCES := $(shell find $(SRC_DIR) -name '*.cpp')
OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

# The program's own main.cpp, and everything else. The program links
# inkerface's main(), which calls program(); test binaries bring their
# own main() and link neither.
MAIN_OBJECT := $(OBJ_DIR)/$(SRC_DIR)/main.o
LIB_OBJECTS := $(filter-out $(MAIN_OBJECT),$(OBJECTS))

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINARIES := $(patsubst $(TEST_DIR)/%.cpp,$(OBJ_DIR)/$(TEST_DIR)/%,\
                   $(TEST_SOURCES))

# -MMD -MP writes a .d file beside every .o listing the headers it read,
# which is what makes a header edit trigger a rebuild. inkerface's
# headers come in with -isystem, so this project's warnings judge its own
# code only.
CPPFLAGS += -I$(INC_DIR) \
            $(patsubst -I%,-isystem %,$(call inkerface_config,--cflags)) \
            -MMD -MP
CXXFLAGS += -std=c++20 -Wall -Wextra -pedantic

# inkerface's libraries, linked statically, and the system libraries
# they need.
PROGRAM_LIBS := $(call inkerface_config,--libs)
TEST_LIBS    := $(filter-out -linkerface_main,$(PROGRAM_LIBS))

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

.PHONY: all emulator firmware upload test clean

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
	@echo "built $(PROGRAM) ($(BUILD), the emulator, on inkerface $(shell cat $(INKERFACE_DIR)/VERSION))"

$(LINKED): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(PROGRAM_LIBS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJECTS) \
	    $(TEST_LIBS)

# PlatformIO builds into <root>/build/disciple/pio/<platform>/; the image
# is copied to bin/ like every other finished program.
firmware:
	$(PIO) run --environment $(PLATFORM)
	@mkdir -p $(BIN_DIR)
	@cp -f $(BUILD_DIR)/pio/$(PLATFORM)/firmware.bin $(FIRMWARE)
	@echo "built $(FIRMWARE) (on inkerface $(shell cat $(INKERFACE_DIR)/VERSION))"

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

# A new version recompiles everything that might print it.
$(OBJECTS): $(VERSION_STAMP)

# Header dependencies discovered by the compiler. Included last, and with
# a leading dash so a first build with no .d files is not an error.
-include $(OBJECTS:.o=.d)
-include $(TEST_BINARIES:=.d)
