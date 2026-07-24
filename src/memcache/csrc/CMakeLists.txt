# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

if (BUILD_PYTHON STREQUAL "ON")
    find_package(pybind11 REQUIRED)
endif ()
include_directories(
        ${PROJECT_MMC_SRC_BASE}/include/
        ${PROJECT_MMC_SRC_BASE}/include/cpp/
        ${PROJECT_MMC_SRC_BASE}/csrc/
        ${PROJECT_MMC_SRC_BASE}/csrc/common
        ${PROJECT_MMC_SRC_BASE}/csrc/config
        ${PROJECT_MMC_SRC_BASE}/csrc/client
        ${PROJECT_MMC_SRC_BASE}/csrc/client/metric
        ${PROJECT_MMC_SRC_BASE}/csrc/local_service
        ${PROJECT_MMC_SRC_BASE}/csrc/entities
        ${PROJECT_MMC_SRC_BASE}/csrc/ha
        ${PROJECT_MMC_SRC_BASE}/csrc/meta_service
        ${PROJECT_MMC_SRC_BASE}/csrc/kv_event
        ${PROJECT_MMC_SRC_BASE}/csrc/log
        ${PROJECT_MMC_SRC_BASE}/csrc/net
        ${PROJECT_MMC_SRC_BASE}/csrc/net/acc_links_impl
        ${PROJECT_MMC_SRC_BASE}/csrc/proto
        ${PROJECT_MMC_SRC_BASE}/csrc/under_api/mf_smem
        ${PROJECT_MMC_SRC_BASE}/csrc/under_api/ubs_io
        ${PROJECT_SMEM_SRC_BASE}/include/host
        ${PROJECT_SMEM_SRC_BASE}/csrc/common
        ${PROJECT_SMEM_SRC_BASE}/csrc/config_store
        ${PROJECT_UTIL_SRC_BASE}/ptracer/include
        ${PROJECT_ACCLINKS_SRC_BASE}/include
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include/
        ${PROJECT_3RDPARTY_SRC_DIR}/log/spdlog/include
        ${PROJECT_3RDPARTY_SRC_DIR}/nlohmann/json/single_include/
        ${PROJECT_3RDPARTY_SRC_DIR}/prometheus-cpp-lite/simpleapi/include
        ${PROJECT_3RDPARTY_SRC_DIR}/prometheus-cpp-lite/core/include
)

if (BUILD_TESTS STREQUAL "ON")
    include_directories(
            ${PROJECT_SOURCE_DIR}/test/ut/testcase/memcache/include)
endif ()

if (BUILD_PYTHON STREQUAL "ON")
    include_directories(
            ${PYTHON_INCLUDE_DIR}
            ${PYTHON_PYBIND11_HOME}/include/
    )
endif ()

## define src variables
set(MMC_SRC_COMMON "")
set(MMC_SRC_CLIENT "")
set(MMC_SRC_LOCAL_SERVICE "")
set(MMC_SRC_ENTITIES "")
set(MMC_SRC_HA "")
set(MMC_SRC_META_SERVICE "")
set(MMC_SRC_NET "")
set(MMC_SRC_PROTO "")
set(MMC_SRC_UNDER_API_MF_SMEM "")
set(MMC_SRC_UNDER_API_UBS_IO "")
set(MMC_SRC_KV_EVENT_RUNTIME "")

## add files into src variables
aux_source_directory(common MMC_SRC_COMMON)
aux_source_directory(config MMC_SRC_CONFIG)
aux_source_directory(client MMC_SRC_CLIENT)
aux_source_directory(client/metric MMC_SRC_CLIENT_METRIC)
aux_source_directory(local_service MMC_SRC_LOCAL_SERVICE)
aux_source_directory(entities MMC_SRC_ENTITIES)
aux_source_directory(ha MMC_SRC_HA)
aux_source_directory(meta_service MMC_SRC_META_SERVICE)
aux_source_directory(net MMC_SRC_NET)
aux_source_directory(log MMC_SRC_LOG)
aux_source_directory(net/acc_links_impl MMC_SRC_NET_ACC)
aux_source_directory(proto MMC_SRC_PROTO)
aux_source_directory(under_api/mf_smem MMC_SRC_UNDER_API_MF_SMEM)
aux_source_directory(under_api/ubs_io MMC_SRC_UNDER_API_UBS_IO)

# KV cache event publisher (opt-in). Default OFF keeps existing build untouched
# and introduces no hard dependency on system libzmq / msgpack.
set(MMC_SRC_KV_EVENT "")
if (MMC_ENABLE_KV_EVENTS)
    aux_source_directory(kv_event MMC_SRC_KV_EVENT)
    if (NOT MMC_KV_EVENTS_MSGPACK_INCLUDE_DIR OR NOT MMC_KV_EVENTS_ZMQ_LIBRARY)
        message(FATAL_ERROR "MMC_ENABLE_KV_EVENTS=ON but 3rdparty kv_events deps not configured. "
                            "Run: git submodule update --init 3rdparty/msgpack-c 3rdparty/libzmq")
    endif ()
    include_directories(${MMC_KV_EVENTS_MSGPACK_INCLUDE_DIR} ${MMC_KV_EVENTS_ZMQ_INCLUDE_DIR})
    message(STATUS "kv_events enabled: zmq=${MMC_KV_EVENTS_ZMQ_LIBRARY} msgpack=${MMC_KV_EVENTS_MSGPACK_INCLUDE_DIR}")
else ()
    set(MMC_SRC_KV_EVENT_RUNTIME kv_event/mmc_kv_event_runtime.cpp)
endif ()

# set whole source for platform
set(PLATFORM_SRC ${MMC_SRC_COMMON}
        ${MMC_SRC_CONFIG}
        ${MMC_SRC_CLIENT}
        ${MMC_SRC_CLIENT_METRIC}
        ${MMC_SRC_LOCAL_SERVICE}
        ${MMC_SRC_ENTITIES}
        ${MMC_SRC_HA}
        ${MMC_SRC_META_SERVICE}
        ${MMC_SRC_NET}
        ${MMC_SRC_LOG}
        ${MMC_SRC_NET_ACC}
        ${MMC_SRC_PROTO}
        ${MMC_SRC_UNDER_API_MF_SMEM}
        ${MMC_SRC_UNDER_API_UBS_IO}
        ${MMC_SRC_KV_EVENT_RUNTIME}
        ${MMC_SRC_KV_EVENT}
        mmc.cpp
        mmc_client.cpp
        mmcache_store.cpp
        mmc_service.cpp)

## build the static and shared library
add_library(mmc_objects OBJECT ${PLATFORM_SRC})
target_include_directories(mmc_objects BEFORE PUBLIC ${PROJECT_MMC_SRC_BASE}/csrc/under_api/mf_smem)
target_link_libraries(mmc_objects PUBLIC config_store_object)
target_compile_options(mmc_objects PRIVATE -fPIC -Wno-float-equal)
if (MMC_ENABLE_KV_EVENTS)
    target_compile_definitions(mmc_objects PRIVATE ZMQ_STATIC)
endif ()
# 注：MMC_ENABLE_KV_EVENTS / MSGPACK_NO_BOOST 已在顶层 add_compile_definitions 全局开启（ON 时），
# 保证库与测试 TU 类布局一致。

add_library(mmc_static STATIC $<TARGET_OBJECTS:mmc_objects> $<TARGET_OBJECTS:ptracer_object> $<TARGET_OBJECTS:config_store_object>)
set_target_properties(mmc_static PROPERTIES OUTPUT_NAME "mf_memcache")
target_link_libraries(mmc_static PUBLIC pthread acc_tcp_net_static prometheus-cpp-simpleapi)
if (MMC_ENABLE_KV_EVENTS)
    target_link_libraries(mmc_static PUBLIC ${MMC_KV_EVENTS_ZMQ_LIBRARY})
endif ()
target_link_directories(mmc_static PUBLIC ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/lib)
target_link_directories(mmc_static PUBLIC ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/lib64)
if (BUILD_PYTHON STREQUAL "ON")
    target_link_libraries(mmc_static PRIVATE pybind11::headers)
endif ()
if (x${CMAKE_BUILD_TYPE} STREQUAL "xDEBUG")
    target_link_libraries(mmc_static PUBLIC pthread libspdlogd.a)
else (
        target_link_libraries (mmc_static PUBLIC pthread libspdlog.a)
)
endif ()

add_library(mmc_shared SHARED $<TARGET_OBJECTS:mmc_objects> $<TARGET_OBJECTS:ptracer_object> $<TARGET_OBJECTS:config_store_object>)
set_target_properties(mmc_shared PROPERTIES OUTPUT_NAME "mf_memcache")
target_link_libraries(mmc_shared PUBLIC pthread acc_tcp_net_static prometheus-cpp-simpleapi)
if (MMC_ENABLE_KV_EVENTS)
    target_link_libraries(mmc_shared PUBLIC ${MMC_KV_EVENTS_ZMQ_LIBRARY})
endif ()
target_link_directories(mmc_shared PUBLIC ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/lib)
target_link_directories(mmc_shared PUBLIC ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/lib64)
if (x${CMAKE_BUILD_TYPE} STREQUAL "xDEBUG")
    target_link_libraries(mmc_shared PUBLIC pthread libspdlogd.a)
else (
        target_link_libraries (mmc_shared PUBLIC pthread libspdlog.a)
)
endif ()

if (BUILD_PYTHON STREQUAL "ON")
    add_subdirectory(python_wrapper)
endif ()
add_subdirectory(daemon)

#install
file(GLOB HEADER_FILES ../include/*.h)
file(GLOB CPP_HEADER_FILES ../include/cpp/*.h)

#install head files
install(FILES ${HEADER_FILES} DESTINATION ${TARGET_INSTALL_DIR}/memcache/include)
install(FILES ${CPP_HEADER_FILES} DESTINATION ${TARGET_INSTALL_DIR}/memcache/include/cpp)

# install library
install(TARGETS mmc_shared
        LIBRARY DESTINATION ${TARGET_INSTALL_DIR}/memcache/lib64
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
)
