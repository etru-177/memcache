####################################################################
# Spdlog
####################################################################
add_subdirectory(log)

####################################################################
# msgpack-cxx (header-only, always available for UT codec/publisher tests)
####################################################################
set(_MMC_MSGPACK_SRC_DIR ${PROJECT_3RDPARTY_SRC_DIR}/msgpack-c)
if (NOT EXISTS "${_MMC_MSGPACK_SRC_DIR}/include/msgpack.hpp")
    message(FATAL_ERROR
        "3rdparty msgpack-c submodule not found at ${_MMC_MSGPACK_SRC_DIR}.\n"
        "Run: git submodule update --init 3rdparty/msgpack-c")
endif ()
set(MMC_KV_EVENTS_MSGPACK_INCLUDE_DIR "${_MMC_MSGPACK_SRC_DIR}/include" CACHE INTERNAL
    "msgpack-cxx header root (3rdparty submodule)")

####################################################################
# KV Events ZMQ dep (libzmq static build), MMC_ENABLE_KV_EVENTS=ON only
####################################################################
if (MMC_ENABLE_KV_EVENTS)
    set(_MMC_ZMQ_SRC_DIR ${PROJECT_3RDPARTY_SRC_DIR}/libzmq)
    set(_MMC_ZMQ_BUILD_DIR ${PROJECT_BUILD_PATH}/build/libzmq)
    set(_MMC_ZMQ_INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/libzmq)

    if (NOT EXISTS "${_MMC_ZMQ_SRC_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "3rdparty libzmq submodule not found at ${_MMC_ZMQ_SRC_DIR}.\n"
            "Run: git submodule update --init 3rdparty/libzmq")
    endif ()

    set(MMC_KV_EVENTS_ZMQ_INCLUDE_DIR "${_MMC_ZMQ_INSTALL_DIR}/include" CACHE INTERNAL
        "libzmq headers (built from 3rdparty submodule)")
    set(_MMC_ZMQ_STATIC_LIB "${_MMC_ZMQ_INSTALL_DIR}/lib/libzmq.a")

    if (EXISTS "${_MMC_ZMQ_STATIC_LIB}")
        message(STATUS "kv_events 3rdparty: libzmq already built at ${_MMC_ZMQ_STATIC_LIB}")
    else ()
        message(STATUS "kv_events 3rdparty: building libzmq (static) from ${_MMC_ZMQ_SRC_DIR}")
        file(MAKE_DIRECTORY "${_MMC_ZMQ_BUILD_DIR}")
        file(MAKE_DIRECTORY "${_MMC_ZMQ_INSTALL_DIR}")

        find_program(_MMC_NINJA_EXE NAMES ninja)
        if (_MMC_NINJA_EXE)
            set(_MMC_ZMQ_GENERATOR "Ninja")
            set(_MMC_ZMQ_MAKE_CMD ninja)
        else ()
            set(_MMC_ZMQ_GENERATOR "Unix Makefiles")
            set(_MMC_ZMQ_MAKE_CMD make)
        endif ()
        if (CMAKE_BUILD_TYPE MATCHES "DEBUG")
            set(_MMC_ZMQ_BUILD_TYPE Debug)
        else ()
            set(_MMC_ZMQ_BUILD_TYPE Release)
        endif ()

        execute_process(
            COMMAND ${CMAKE_COMMAND}
                -G "${_MMC_ZMQ_GENERATOR}"
                -DCMAKE_BUILD_TYPE=${_MMC_ZMQ_BUILD_TYPE}
                -DCMAKE_INSTALL_PREFIX=${_MMC_ZMQ_INSTALL_DIR}
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED=OFF
                -DBUILD_STATIC=ON
                -DBUILD_TESTS=OFF
                -DENABLE_DRAFTS=OFF
                -DENABLE_WS=OFF
                -DENABLE_CURVE=OFF
                -DWITH_LIBSODIUM=OFF
                -DWITH_TLS=OFF
                -DWITH_DOCS=OFF
                -S ${_MMC_ZMQ_SRC_DIR}
                -B ${_MMC_ZMQ_BUILD_DIR}
            RESULT_VARIABLE _MMC_ZMQ_CMAKE_RESULT
            OUTPUT_VARIABLE _MMC_ZMQ_CMAKE_OUT
            ERROR_VARIABLE _MMC_ZMQ_CMAKE_ERR
        )
        if (NOT _MMC_ZMQ_CMAKE_RESULT EQUAL 0)
            message(FATAL_ERROR "libzmq cmake configure failed:\n${_MMC_ZMQ_CMAKE_OUT}\n${_MMC_ZMQ_CMAKE_ERR}")
        endif ()
        execute_process(
            COMMAND ${_MMC_ZMQ_MAKE_CMD} -j8
            WORKING_DIRECTORY ${_MMC_ZMQ_BUILD_DIR}
            RESULT_VARIABLE _MMC_ZMQ_BUILD_RESULT
            OUTPUT_VARIABLE _MMC_ZMQ_BUILD_OUT
            ERROR_VARIABLE _MMC_ZMQ_BUILD_ERR
        )
        if (NOT _MMC_ZMQ_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "libzmq build failed:\n${_MMC_ZMQ_BUILD_OUT}\n${_MMC_ZMQ_BUILD_ERR}")
        endif ()
        execute_process(
            COMMAND ${_MMC_ZMQ_MAKE_CMD} install
            WORKING_DIRECTORY ${_MMC_ZMQ_BUILD_DIR}
            RESULT_VARIABLE _MMC_ZMQ_INSTALL_RESULT
            OUTPUT_VARIABLE _MMC_ZMQ_INSTALL_OUT
            ERROR_VARIABLE _MMC_ZMQ_INSTALL_ERR
        )
        if (NOT _MMC_ZMQ_INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "libzmq install failed:\n${_MMC_ZMQ_INSTALL_OUT}\n${_MMC_ZMQ_INSTALL_ERR}")
        endif ()
        if (NOT EXISTS "${_MMC_ZMQ_STATIC_LIB}")
            set(_MMC_ZMQ_STATIC_LIB64 "${_MMC_ZMQ_INSTALL_DIR}/lib64/libzmq.a")
            if (EXISTS "${_MMC_ZMQ_STATIC_LIB64}")
                file(MAKE_DIRECTORY "${_MMC_ZMQ_INSTALL_DIR}/lib")
                file(COPY "${_MMC_ZMQ_STATIC_LIB64}" DESTINATION "${_MMC_ZMQ_INSTALL_DIR}/lib")
            else ()
                message(FATAL_ERROR "libzmq static library not found after install (expected ${_MMC_ZMQ_STATIC_LIB})")
            endif ()
        endif ()
        message(STATUS "kv_events 3rdparty: libzmq installed to ${_MMC_ZMQ_INSTALL_DIR}")
    endif ()

    set(MMC_KV_EVENTS_ZMQ_LIBRARY "${_MMC_ZMQ_STATIC_LIB}" CACHE INTERNAL "libzmq static library (3rdparty)")
    message(STATUS "kv_events 3rdparty: msgpack=${MMC_KV_EVENTS_MSGPACK_INCLUDE_DIR} zmq=${MMC_KV_EVENTS_ZMQ_LIBRARY}")
endif ()

####################################################################
# MemFabric_Hybrid
####################################################################

set(_ACTUAL_BUILD_TESTS ${BUILD_TESTS})
set(BUILD_TESTS OFF)
set(BUILD_FROM_MEMCACHE ON)
add_subdirectory(memfabric_hybrid)
set(BUILD_TESTS ${_ACTUAL_BUILD_TESTS})
