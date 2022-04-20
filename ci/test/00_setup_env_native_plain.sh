#!/usr/bin/env bash
#
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export CONTAINER_NAME=ci_native_plain
export DOCKER_NAME_TAG=ubuntu:20.04
export GOAL="install"
export BITCOIN_CONFIG="--disable-tests --disable-bench"  # Use clang to avoid OOM

# disabled until unit & functional tests are fully ported
export RUN_UNIT_TESTS=false
export RUN_FUNCTIONAL_TESTS=false
