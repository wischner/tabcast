#
# Apostle build.
#
# Apostle is the Tabcast broadcast daemon: it accepts frames from the
# Evangelist browser extension on loopback and fans them out to every
# registered Disciple on the network.
#
# Artifacts do not stay here. This is one project in a multi-project
# repository, and the repository has exactly one build/ and one bin/,
# both at its root. Objects land in build/apostle/<build type>/ and the
# finished program in bin/apostle. Building from inside this directory
# works the same way, because the paths are relative to ROOT_DIR.
#
# Two build types. `debug`, the default, carries full warnings and the
# address and undefined behaviour sanitizers. `release` links the C++
# runtime into the binary so the daemon ships as one file with nothing
# to install beside it.
#
#   make                 debug build into <root>/bin/apostle
#   make BUILD=release   release build into <root>/bin/apostle
#   make test            build and run the tests
#   make help            list every target
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
# lib/<name>/, and are found rather than listed, so adding a file to
# the tree is all it takes to get it built.
SOURCES := $(shell find $(SRC_DIR) $(LIB_DIR) -name '*.cpp' 2>/dev/null)
OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

# The entry point, and everything that is not the entry point. Test
# binaries bring their own main(), so they link the second set only.
MAIN_OBJECT := $(OBJ_DIR)/$(SRC_DIR)/main.o
LIB_OBJECTS := $(filter-out $(MAIN_OBJECT),$(OBJECTS))

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINARIES := $(patsubst $(TEST_DIR)/%.cpp,$(OBJ_DIR)/$(TEST_DIR)/%,\
                   $(TEST_SOURCES))

# -MMD -MP writes a .d file beside every .o listing the headers it
# read, which is what makes a header edit trigger a rebuild.
CPPFLAGS += -I$(INC_DIR) -MMD -MP
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

.PHONY: all release run test clean distclean help

## all: build the daemon into the repository bin/ (default)
all: $(LINKED)
	@mkdir -p $(BIN_DIR)
	@cp -f $< $(PROGRAM)
	@echo "built $(PROGRAM) ($(BUILD))"

## release: shorthand for BUILD=release
release:
	@$(MAKE) --no-print-directory BUILD=release

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

## run: build and run the daemon
run: all
	@$(PROGRAM)

## test: build and run every test in tests/
test: $(TEST_BINARIES)
ifeq ($(strip $(TEST_SOURCES)),)
	@echo "no tests yet, add a .cpp with a main() to $(TEST_DIR)/"
else
	@for t in $(TEST_BINARIES); do \
	    echo "running $$t"; \
	    $$t || exit 1; \
	done
endif

## clean: remove this project's artifacts for the current build type
clean:
	@rm -rf $(BUILD_DIR)/$(BUILD)

# Removes only what this project produced. bin/ is shared with the
# other parts of the repository, so the program is deleted by name
# rather than by emptying the directory.
## distclean: remove all of this project's artifacts and its program
distclean:
	@rm -rf $(BUILD_DIR) $(PROGRAM)

## help: list the targets
help:
	@grep -hE '^## ' $(firstword $(MAKEFILE_LIST)) | sed 's/^## /  /'

# Header dependencies discovered by the compiler. Included last, and
# with a leading dash so a first build with no .d files is not an
# error.
-include $(OBJECTS:.o=.d)
-include $(TEST_BINARIES:=.d)
