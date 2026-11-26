#
# Apostle build. Run by the top-level Makefile; use that one.
#
# Apostle is the Tabcast broadcast daemon: it accepts frames from the
# Evangelist browser extension on loopback and fans them out to every
# registered Disciple on the network.
#
# Objects land in <root>/build/apostle/<build type>/ and the program in
# <root>/bin/apostle, never beside the sources.
#
# Two build types. `debug` carries full warnings and the address and
# undefined behaviour sanitizers. `release` links the C++ runtime into
# the binary so the daemon ships as one file with nothing to install
# beside it.
#
# Targets: all (the default), test, clean.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

NAME    := apostle
BUILD   ?= debug

# System-wide preload libraries such as AppProtection can prevent ASan
# from starting. Use SANITIZERS=undefined for debugging on those hosts.
SANITIZERS ?= address,undefined

# Where the repository root is, relative to this project. Everything
# generated goes there and nothing is written beside the sources.
ROOT_DIR ?= ..

SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests

BUILD_DIR := $(ROOT_DIR)/build/$(NAME)
BIN_DIR   := $(ROOT_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/$(BUILD)
ifeq ($(BUILD),debug)
    OBJ_DIR := $(OBJ_DIR)/$(SANITIZERS)
endif

# stb_image, downloaded by the top-level Makefile.
STB_DIR ?= $(abspath $(ROOT_DIR)/deps/stb)

ifeq ($(wildcard $(STB_DIR)/stb_image.h)$(filter clean,$(MAKECMDGOALS)),)
    $(error stb_image is not in $(STB_DIR); run make at the repository root, which downloads it)
endif

CXX ?= g++

# Every translation unit of the program, found rather than listed, so
# adding a file to src/ is all it takes to get it built.
SOURCES := $(shell find $(SRC_DIR) -name '*.cpp')
OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

# The entry point, and everything that is not the entry point. Test
# binaries bring their own main(), so they link the second set only.
MAIN_OBJECT := $(OBJ_DIR)/$(SRC_DIR)/main.o
LIB_OBJECTS := $(filter-out $(MAIN_OBJECT),$(OBJECTS))

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINARIES := $(patsubst $(TEST_DIR)/%.cpp,$(OBJ_DIR)/$(TEST_DIR)/%,\
                   $(TEST_SOURCES))

# -MMD -MP writes a .d file beside every .o listing the headers it
# read, which is what makes a header edit trigger a rebuild. stb_image
# comes in with -isystem, as third-party code.
CPPFLAGS += -I$(INC_DIR) -isystem $(STB_DIR) -MMD -MP
CXXFLAGS += -std=c++20 -Wall -Wextra -pedantic

ifeq ($(BUILD),debug)
    CXXFLAGS += -g -Og -fno-omit-frame-pointer \
                -fsanitize=$(SANITIZERS)
    LDFLAGS  += -fsanitize=$(SANITIZERS)
else ifeq ($(BUILD),release)
    CXXFLAGS += -O2 -DNDEBUG
    # One self-contained executable. Not a full -static link: glibc
    # resolves hostnames through dlopened NSS modules, so a fully
    # static binary would fail to look anything up at runtime.
    LDFLAGS  += -static-libgcc -static-libstdc++
else
    $(error BUILD must be debug or release, got '$(BUILD)')
endif

# The link lands in build/, where every artifact belongs, and the
# finished program is copied to bin/. That keeps bin/ correct when
# switching build types, which a plain timestamp rule would not.
LINKED  := $(OBJ_DIR)/$(NAME)
PROGRAM := $(BIN_DIR)/$(NAME)

.PHONY: all test clean

all: $(LINKED)
	@mkdir -p $(BIN_DIR)
	@cp -f $< $(PROGRAM)
	@echo "built $(PROGRAM) ($(BUILD))"

$(LINKED): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJECTS) \
	    $(LDLIBS)

test: $(TEST_BINARIES)
ifeq ($(strip $(TEST_SOURCES)),)
	@echo "no tests yet, add a .cpp with a main() to $(TEST_DIR)/"
else
	@for t in $(TEST_BINARIES); do \
	    echo "running $$t"; \
	    $$t || exit 1; \
	done
endif

clean:
	@rm -rf $(BUILD_DIR) $(PROGRAM)

# Header dependencies discovered by the compiler. Included last, and
# with a leading dash so a first build with no .d files is not an
# error.
-include $(OBJECTS:.o=.d)
-include $(TEST_BINARIES:=.d)
