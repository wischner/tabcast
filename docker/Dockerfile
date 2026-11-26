# The Tabcast build environment.
#
# Every tool a Tabcast build needs lives in this image, so a machine that
# builds Tabcast needs only make and Docker, and stays as it was. The
# top-level Makefile builds the image the first time and runs every
# build in it with the repository mounted at the same path, as the
# calling user, so what it writes is that user's and lands where it
# would without Docker.
#
# Ubuntu 26.04, the release inkerface's own images build on: Disciple
# links inkerface's prebuilt libraries, and those want the compiler and
# C library they were built with. The programs built here, and the
# Debian package, therefore run on Ubuntu 26.04 and newer.
#
# The image carries tools only. What a build downloads, inkerface's
# release, stb, and PlatformIO's toolchains, goes to the repository's
# deps/ directory, so it is fetched once and survives `make clean`.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

# C++ compiler and make for Apostle and Disciple. OpenSSL and X11
# headers, and pkg-config, to link inkerface's release. curl and Python 3
# for downloads; Python 3 also for the extension's build and PlatformIO.
# git, which PlatformIO uses for some packages. Node.js only for the
# extension's tests. dpkg-dev for the package.
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        dpkg-dev \
        git \
        libssl-dev \
        libx11-dev \
        make \
        nodejs \
        pkg-config \
        python3 \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

# PlatformIO builds and flashes reader firmware, the version inkerface's
# releases are built with. Its toolchains are downloaded per board on
# first use, into deps/platformio (PLATFORMIO_CORE_DIR).
ARG PLATFORMIO_VERSION=6.2.0
RUN python3 -m venv /opt/platformio \
    && /opt/platformio/bin/pip install --no-cache-dir \
        "platformio==${PLATFORMIO_VERSION}"

ENV PATH=/opt/platformio/bin:$PATH

# Builds run as the calling user, who has no home in the image.
ENV HOME=/tmp \
    PLATFORMIO_DISABLE_PROGRESSBAR=true
