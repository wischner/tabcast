#
# Disciple build.
#
# Disciple is the Tabcast e-ink client: it registers with the Apostle
# daemon, receives broadcast frames, and renders them on the panel
# through the FreeInk SDK. The first target device is the XTEINK X4.
#
# Artifacts do not stay here. This is one project in a multi-project
# repository, and the repository has exactly one build/ and one bin/,
# both at its root. Objects land in build/disciple/<build type>/ and
# the finished program in bin/disciple. Building from inside this
# directory works the same way, because the paths are relative to
# ROOT_DIR.
#
# Two build types. `debug`, the default, carries full warnings and the
# address and undefined behaviour sanitizers. `release` links the C++
# runtime into the binary so the client ships as one file that can be
# copied onto a reader with nothing to install beside it.
#
# BACKEND picks what the client can draw on:
#
#   test      a window on this machine, through gtkmm, the C++ binding
#             for GTK 3. This is the test client, and it renders a page
#             exactly as the reader will. Chosen automatically when
#             gtkmm is installed.
#   none      no display and no image decoder, no dependencies at all.
#             The protocol and the panel model still build and test.
#   freeink   the real panel on the device, through the FreeInk SDK.
#
#   make                 debug build into <root>/bin/disciple
#   make BUILD=release   release build into <root>/bin/disciple
#   make BACKEND=none    build with no dependencies
#   make test            build and run the tests
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

# The test client is built on gtkmm, the C++ binding for GTK 3, found
# through pkg-config like any other system library. On Debian and
# Ubuntu it is the libgtkmm-3.0-dev package.
GTKMM ?= gtkmm-3.0

# Default to the windowed test client when gtkmm is installed, and to a
# dependency-free build when it is not. That way a fresh checkout still
# builds with a bare `make` at the repository root, without silently
# skipping the display when it could have had one.
BACKEND ?= $(if $(shell pkg-config --exists $(GTKMM) && echo yes),test,none)

# Where the repository root is, relative to this project. Everything
# generated goes there and nothing is written beside the sources.
ROOT_DIR ?= ..

SRC_DIR  := src
LIB_DIR  := lib
INC_DIR  := include
TEST_DIR := tests

BUILD_DIR := $(ROOT_DIR)/build/$(NAME)
BIN_DIR   := $(ROOT_DIR)/bin
# Keep backend objects and links separate so installing gtkmm or
# switching BACKEND cannot reuse a previously linked headless client.
OBJ_DIR   := $(BUILD_DIR)/$(BUILD)/$(BACKEND)
ifeq ($(BUILD),debug)
    OBJ_DIR := $(OBJ_DIR)/$(SANITIZERS)
endif

CXX ?= g++

# Every translation unit of the program. Sources live in src/ and in
# lib/<name>/, and are found rather than listed, so adding a file to
# the tree is all it takes to get it built.
ALL_SOURCES := $(shell find $(SRC_DIR) $(LIB_DIR) -name '*.cpp' \
                 2>/dev/null)

# Keep only this build's backend.
OTHER_SOURCES := $(filter-out $(SRC_DIR)/backend_$(BACKEND).cpp, \
    $(wildcard $(SRC_DIR)/backend_*.cpp))

SOURCES := $(filter-out $(OTHER_SOURCES),$(ALL_SOURCES))
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

# Exactly one backend is compiled. Every other backend's source is
# excluded from SOURCES above, so an uninstalled dependency cannot
# break a build that does not use it.
ifeq ($(BACKEND),test)
    GTKMM_LIBS := $(shell pkg-config --libs $(GTKMM) 2>/dev/null)
    ifeq ($(strip $(GTKMM_LIBS)),)
        $(error BACKEND=test needs $(GTKMM). Install libgtkmm-3.0-dev, \
            or build with BACKEND=none)
    endif
    # -isystem rather than -I, so that -Wall -Wextra -pedantic judges
    # this project's code and not gtkmm's headers.
    CPPFLAGS += $(patsubst -I%,-isystem %,\
                  $(shell pkg-config --cflags $(GTKMM)))
    LDLIBS   += $(GTKMM_LIBS)
else ifeq ($(BACKEND),freeink)
    # Point FREEINK_DIR at an unpacked SDK. It is linked statically,
    # like every other dependency.
    FREEINK_DIR ?= /opt/freeink
    CPPFLAGS += -I$(FREEINK_DIR)/include
    LDFLAGS  += -L$(FREEINK_DIR)/lib
    LDLIBS   += -l:libfreeink.a
else ifneq ($(BACKEND),none)
    $(error BACKEND must be test, none, or freeink, got '$(BACKEND)')
endif

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

## all: build the client into the repository bin/ (default)
all: $(LINKED)
	@mkdir -p $(BIN_DIR)
	@cp -f $< $(PROGRAM)
	@echo "built $(PROGRAM) ($(BUILD), backend $(BACKEND))"

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

# GTK allocates a good deal at startup and never frees it, which is
# its business and not a bug in this client. A debug build would
# otherwise end every windowed run with a page of leak reports and a
# non-zero exit. Suppressed by library rather than switched off, so a
# leak in Disciple's own code still gets reported.
LSAN_SUPPRESSIONS := $(CURDIR)/scripts/lsan.supp
RUN_ENV := LSAN_OPTIONS=suppressions=$(LSAN_SUPPRESSIONS)

## run: build and run the client
run: all
	@$(RUN_ENV) $(PROGRAM)

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
