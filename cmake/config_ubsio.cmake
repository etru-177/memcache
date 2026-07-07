# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

####################################################################
# ubs-io (SSD backend)
####################################################################
message(STATUS "BUILD_UBSIO = ${BUILD_UBSIO}")
if (BUILD_UBSIO)
    set(UBSIO_OUTPUT_DIR ${PROJECT_OUTPUT_PATH}/3rdparty/ubsio)

    # skip if already built
    file(GLOB UBSIO_EXISTING_SO "${UBSIO_OUTPUT_DIR}/lib/*.so*")
    if (UBSIO_EXISTING_SO)
        message(STATUS "ubs-io already built, skip (found ${UBSIO_OUTPUT_DIR}/lib/*.so)")
        return()
    endif ()

    include(FetchContent)

    set(UBSIO_SRC_DIR "${FETCHCONTENT_BASE_DIR}/ubs-io-src")
    if (EXISTS "${UBSIO_SRC_DIR}/ubsio-boostio" AND EXISTS "${UBSIO_SRC_DIR}/ubsio-kv")
        message(STATUS "ubs-io local source found at ${UBSIO_SRC_DIR}, skip clone")
        FetchContent_Declare(ubs-io SOURCE_DIR ${UBSIO_SRC_DIR})
    else()
        FetchContent_Declare(
            ubs-io
            GIT_REPOSITORY https://gitcode.com/openeuler/ubs-io.git
            GIT_TAG develop
        )
    endif()

    FetchContent_GetProperties(ubs-io)
    if (NOT ubs-io_POPULATED)
        FetchContent_Populate(ubs-io)
    endif ()

    message(STATUS "ubs-io source dir: ${ubs-io_SOURCE_DIR}")

    message(STATUS "Building ubs-io boostio...")
    execute_process(
        COMMAND bash ${ubs-io_SOURCE_DIR}/ubsio-boostio/build.sh -t release
        WORKING_DIRECTORY ${ubs-io_SOURCE_DIR}/ubsio-boostio
        RESULT_VARIABLE UBSIO_BOOSTIO_RESULT
    )
    if (NOT UBSIO_BOOSTIO_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build ubsio-boostio")
    endif ()

    message(STATUS "Building ubs-io kv...")
    execute_process(
        COMMAND bash ${ubs-io_SOURCE_DIR}/ubsio-kv/build.sh -t release
        WORKING_DIRECTORY ${ubs-io_SOURCE_DIR}/ubsio-kv
        RESULT_VARIABLE UBSIO_KV_RESULT
    )
    if (NOT UBSIO_KV_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build ubsio-kv")
    endif ()

    message(STATUS "ubs-io build completed")

    # install .so files to project output (mirrors hcom: output/3rdparty/hcom/lib/)
    file(MAKE_DIRECTORY ${UBSIO_OUTPUT_DIR}/lib)
    file(GLOB UBSIO_SO_FILES
        ${ubs-io_SOURCE_DIR}/ubsio-boostio/dist/boostio/lib/*.so*
        ${ubs-io_SOURCE_DIR}/ubsio-boostio/dist/3rdparty/libboundscheck/lib/*.so*
        ${ubs-io_SOURCE_DIR}/ubsio-boostio/dist/3rdparty/libaio/lib/*.so*
        ${ubs-io_SOURCE_DIR}/ubsio-kv/dist/lib/*.so*)
    file(COPY ${UBSIO_SO_FILES} DESTINATION ${UBSIO_OUTPUT_DIR}/lib)
    message(STATUS "ubs-io lib installed to ${UBSIO_OUTPUT_DIR}/lib")

    # install bio.conf, rename to ubsio.conf
    file(MAKE_DIRECTORY ${UBSIO_OUTPUT_DIR}/conf)
    file(COPY_FILE ${ubs-io_SOURCE_DIR}/ubsio-boostio/configs/bio.conf
         ${UBSIO_OUTPUT_DIR}/conf/ubsio.conf)
    message(STATUS "ubs-io config installed to ${UBSIO_OUTPUT_DIR}/conf")
endif ()
