#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

# =============================================================================
# ci_devcontainer_check.sh — CI validation via real Dockerfile + post_create.sh
# =============================================================================
# Reuses existing files directly — does NOT re-implement the flow:
#   1. docker build  ->  .devcontainer/Dockerfile
#   2. docker run    ->  .devcontainer/post_create.sh   (inside container)
#   3. docker run    ->  script/run_all_examples.sh      (inside same container)
#
# Docker run args (mounts, devices, env) mirror .devcontainer/devcontainer.json.
#
# Exit codes:
#   0 = all steps passed
#   1 = prerequisite check or docker build/run failed
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

IMAGE_NAME="memcache-ci:latest"

# -- Helpers ------------------------------------------------------------------
info()  { echo -e "  \e[36m[CI]\e[0m  $*"; }
ok()    { echo -e "  \e[32m[OK]\e[0m  $*"; }
err()   { echo -e "  \e[31m[ERR]\e[0m  $*"; }
header(){ echo -e "\n\e[1;34m--- $* ---\e[0m"; }

# -- Step 1: Check prerequisites ----------------------------------------------
check_prerequisites() {
    if ! command -v docker &>/dev/null; then
        err "docker not found. Install Docker or use a CI runner with Docker support."
        exit 1
    fi
    ok "docker: $(docker --version)"

    local npu_count
    npu_count=$(ls -d /dev/davinci[0-9]* 2>/dev/null | wc -l || true)
    if [[ "$npu_count" -eq 0 ]]; then
        err "No NPU devices found on host (/dev/davinci*)."
        exit 1
    fi
    ok "NPU: $npu_count device(s) on host"

    # Bind mount sources from devcontainer.json — must exist on host
    local mount_paths=(
        /usr/local/dcmi
        /usr/local/Ascend/driver/tools/hccn_tool
        /usr/local/bin/npu-smi
        /usr/local/Ascend/driver/lib64/
        /usr/local/Ascend/driver/version.info
        /etc/ascend_install.info
        /etc/hccn.conf
    )
    for p in "${mount_paths[@]}"; do
        if [[ ! -e "$p" ]]; then
            err "Bind mount source not found: $p"
            err "Ensure CANN driver is installed on the host."
            exit 1
        fi
    done
    ok "All bind mount sources exist"

    # Verify devcontainer files exist
    local devc_files=(
        "$PROJECT_DIR/.devcontainer/Dockerfile"
        "$PROJECT_DIR/.devcontainer/post_create.sh"
        "$PROJECT_DIR/.devcontainer/requirements.txt"
        "$PROJECT_DIR/script/run_all_examples.sh"
    )
    for f in "${devc_files[@]}"; do
        if [[ ! -f "$f" ]]; then
            err "Required file not found: $f"
            exit 1
        fi
    done
    ok "All devcontainer files present"
}

# -- Step 2: Build Docker image from .devcontainer/Dockerfile -----------------
build_image() {
    info "Building Docker image: $IMAGE_NAME"
    info "  Dockerfile: $PROJECT_DIR/.devcontainer/Dockerfile"

    docker build -t "$IMAGE_NAME" \
        -f "$PROJECT_DIR/.devcontainer/Dockerfile" \
        "$PROJECT_DIR" --network=host
    ok "Image built: $IMAGE_NAME"
}

# -- Step 3: Run container (mirrors devcontainer.json) ------------------------
run_in_container() {
    info "Starting container..."

    local inner="bash .devcontainer/post_create.sh"
    inner+=" && bash script/run_all_examples.sh --verbose"

    # Mounts, devices, env from .devcontainer/devcontainer.json
    local run_args=(
        run --rm
        --network=host
        --privileged
        --shm-size=2g
        -v "$PROJECT_DIR:/workspaces/memcache"
        -v /usr/local/dcmi:/usr/local/dcmi
        -v /usr/local/Ascend/driver/tools/hccn_tool:/usr/local/Ascend/driver/tools/hccn_tool
        -v /usr/local/bin/npu-smi:/usr/local/bin/npu-smi
        -v /usr/local/Ascend/driver/lib64/:/usr/local/Ascend/driver/lib64/
        -v /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info
        -v /etc/ascend_install.info:/etc/ascend_install.info
        -v /etc/hccn.conf:/etc/hccn.conf
        -e PYTHONUNBUFFERED=1
        -e PIP_INDEX_URL=https://mirrors.aliyun.com/pypi/simple/
        -e PIP_TRUSTED_HOST=mirrors.aliyun.com
        -e PRE_COMMIT_HOME=/root/.cache/pre-commit
        -e PIP_SYSTEM_SITE_PACKAGES=1
        -e CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)
        -e MMC_BUILD_JOBS=$(nproc)
        -e PYTHON_HOME=/usr/local/python3.11.15
        -w /workspaces/memcache
    )

    local log_file="/tmp/memcache_ci_$(date +%Y%m%d_%H%M%S).log"
    info "Logging container output to: $log_file"
    # Redirect docker output to the log file (NOT piped through tee) so that a
    # closed/slow stdout cannot break the pipe and SIGPIPE the in-container
    # run_all_examples.sh. A background `tail -f` streams the file to stdout
    # for live visibility; if it dies, docker keeps writing to the file.
    : > "$log_file"
    docker "${run_args[@]}" "$IMAGE_NAME" bash -c "$inner" >"$log_file" 2>&1 &
    local docker_pid=$!
    tail -f "$log_file" 2>/dev/null &
    local tail_pid=$!
    local rc=0
    wait "$docker_pid" || rc=$?
    kill "$tail_pid" 2>/dev/null || true
    wait "$tail_pid" 2>/dev/null || true
    if [[ $rc -ne 0 ]]; then
        err "Container exited with code $rc"
        err "Full log: $log_file"
        exit $rc
    fi
    ok "Container run completed (log: $log_file)"
}

# -- Cleanup stale host MetaService --------------------------------------------
# The container runs with --network=host and shares the host's port 5000.
# run_all_examples.sh reuses any process already listening on port 5000 as the
# MetaService. A stale host MetaService from a previous run is usually a
# different build than the freshly source-built client inside the container,
# so every alloc RPC fails (error -1) and all Python examples fail. Kill any
# stale MemCache MetaService so the container starts its own matching one.
cleanup_stale_meta() {
    local pids
    pids=$(pgrep -f "mmc_meta_service|from memcache_hybrid import MetaService" 2>/dev/null || true)
    if [[ -n "$pids" ]]; then
        info "Killing stale MemCache MetaService on host (PID: $(echo $pids | tr '\n' ' '))"
        kill $pids 2>/dev/null || true
        sleep 2
        kill -9 $pids 2>/dev/null || true
        sleep 1
    fi
}

# -- Main ---------------------------------------------------------------------
echo ""
echo "============================================"
echo " MemCache CI -- DevContainer Check"
echo " Project: $PROJECT_DIR"
echo " Image:   $IMAGE_NAME"
echo "============================================"

header "Step 1: Check prerequisites"
check_prerequisites

header "Step 1.5: Cleanup stale MetaService on host"
cleanup_stale_meta

header "Step 2: Build Docker image (Dockerfile)"
build_image

header "Step 3: Run post_create.sh + run_all_examples.sh (in container)"
run_in_container

echo ""
echo "============================================"
echo " CI DevContainer Check -- PASSED"
echo "============================================"
