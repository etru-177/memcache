# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

# =============================================================================
# MemCache Dev Container — Dockerfile
# =============================================================================
# Purpose:
#   Build a consistent development environment for MemCache based on the Ascend
#   vLLM image (with CANN + NPU driver toolkits pre-installed).
#
# Base image:
#   quay.nju.edu.cn/ascend/vllm-ascend — includes Ascend CANN, NPU drivers,
#   PyTorch, and the vLLM-Ascend ecosystem. It already ships:
#     - gcc 11.4+ (Ubuntu 22.04 default)
#     - cmake 3.22+
#     - ninja 1.13.0
#     - Python 3.11.15
#
# Why Dockerfile instead of postCreateCommand:
#   - System packages (apt-get) and Python deps (pip) are baked into the image
#     layer, so they survive "Rebuild Container" and work offline.
#   - Build-time only: pip cache and apt lists are cleaned in the same RUN layer
#     to keep the final image small.
#   - Project-specific initialisation (git submodules, CMake smoke test) is left
#     to postCreateCommand because it depends on the working tree state.
# =============================================================================

# The official image source is quay.io/ascend/vllm-ascend,
# and the configuration uses domestic Chinese mirrors to speed up downloads.
# This image supports a2. If you use it on other models,
# you'll need to replace it with the corresponding image.
FROM quay.nju.edu.cn/ascend/vllm-ascend:v0.20.2rc1

LABEL maintainer="MemCache Team"
LABEL description="MemCache development container (Ascend NPU)"
LABEL version="1.0.0"


# ---------------------------------------------------------------------------
# Install packages NOT present in the base image
#   build-essential, cmake, ninja, python3, gcc, patchelf(wheel auditwheel repair) — all already shipped.
#   Added:
#     - doxygen, graphviz: API documentation generation
#     - gdb, valgrind, strace, ltrace, cgdb: debugging and profiling
#     - dos2unix: line-ending conversion (used by build.sh, run_ut.sh, run_fuzz.sh)
#     - lcov: code coverage generation (used by run_ut.sh, run_fuzz.sh)
#     - bc: floating-point arithmetic in performance_compare.sh
# ---------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    doxygen \
    graphviz \
    gdb \
    valgrind \
    strace \
    ltrace \
    cgdb \
    dos2unix \
    lcov \
    bc \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Verify toolchain — quick smoke test for the developer's peace of mind
# ---------------------------------------------------------------------------
RUN echo "=== Toolchain versions ===" \
    && gcc --version | head -1 \
    && cmake --version | head -1 \
    && ninja --version \
    && python3 --version \
    && gdb --version | head -1 \
    && valgrind --version \
    && dos2unix -v 2>&1 | head -1 \
    && lcov --version | head -1 \
    && bc --version | head -1

# ---------------------------------------------------------------------------
# Set up a domestic mirror for pip
# ---------------------------------------------------------------------------

# 创建 pip 配置文件
RUN mkdir -p /etc/pip && \
    echo "[global]" > /etc/pip.conf && \
    echo "index-url = https://mirrors.aliyun.com/pypi/simple/" >> /etc/pip.conf && \
    echo "trusted-host = mirrors.aliyun.com" >> /etc/pip.conf

COPY .devcontainer/requirements.txt /tmp/requirements.txt

# ---------------------------------------------------------------------------
# Set working directory — the repo root will be mounted here by VS Code
# ---------------------------------------------------------------------------
WORKDIR /workspaces/memcache
