# The Tabcast build environment.
#
# Every tool a Tabcast build needs lives in this image, so a machine that
# builds Tabcast needs only make and Docker, and stays as it was. The
# top-level Makefile builds the image the first time and runs every
# build in it with the repository mounted at the same path, as the
# calling user, so what it writes is that user's and lands where it
# would without Docker.
#
# Ubuntu 24.04 because the programs built here also run on any newer
# release, and because that is what the Debian package is built for.
#
# The image carries tools only. What a build downloads, inkerface, stb,
# the FreeInk SDK, and PlatformIO's toolchains, goes to the repository's
# deps/ directory, so it is fetched once and survives `make clean`.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# C++ compiler and make for Apostle and Disciple. CMake, git, patch,
# OpenSSL, and X11 headers for inkerface. curl for downloads. Python 3
# for the extension's build, inkerface's downloads, and PlatformIO.
# Node.js only for the extension's tests. dpkg-dev for the package.
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        dpkg-dev \
        git \
        libssl-dev \
        libx11-dev \
        make \
        nodejs \
        patch \
        python3 \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

# PlatformIO builds and flashes reader firmware. The version every
# reader build has been verified with. Its toolchains are downloaded per
# board on first use, into deps/platformio (PLATFORMIO_CORE_DIR).
RUN python3 -m venv /opt/platformio \
    && /opt/platformio/bin/pip install --no-cache-dir platformio==6.1.19

ENV PATH=/opt/platformio/bin:$PATH

# Builds run as the calling user, who has no home in the image.
ENV HOME=/tmp
