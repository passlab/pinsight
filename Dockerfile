# PASSLab OMPT Research Image.
# Copyright (c) PASSLab Team, 2018. All rights reserved.

# Use Ubuntu 16.04 (with proper init system) as the base.
FROM phusion/baseimage:0.11
RUN apt-get update && apt-get dist-upgrade -y

# Set working directory to `/opt`.
WORKDIR /opt

# Install needed tools.
RUN apt-add-repository -y ppa:lttng/stable-2.10 && \
    apt-get update && \
    apt-get install -y \
    build-essential \
    cmake \
    git \
    lttng-tools \
    lttng-modules-dkms \
    liblttng-ust-dev \
    && rm -rf /var/lib/apt/lists/*

# Build OpenMP runtime.
RUN git clone https://github.com/llvm-mirror/openmp.git && \
    cd openmp && \
    git remote update && \
    cd .. && \
    mkdir -p openmp-build && \
    mkdir -p openmp-install && \
    cd openmp-build && \
    cmake -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=../openmp-install \
      -DOPENMP_ENABLE_LIBOMPTARGET=off \
      -DLIBOMP_OMPT_SUPPORT=on \
      ../openmp

RUN cd openmp-build && \
    make && \
    make install
