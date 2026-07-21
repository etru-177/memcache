file(GLOB_RECURSE mmc_test_src
        ${CMAKE_CURRENT_SOURCE_DIR}/meta_service/*
        ${CMAKE_CURRENT_SOURCE_DIR}/local_service/*
        ${CMAKE_CURRENT_SOURCE_DIR}/entities/*
        ${CMAKE_CURRENT_SOURCE_DIR}/common/*
        ${CMAKE_CURRENT_SOURCE_DIR}/config/*
        ${CMAKE_CURRENT_SOURCE_DIR}/log/*
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/*
        ${CMAKE_CURRENT_SOURCE_DIR}/under_api/ubs_io/*
)
list(FILTER mmc_test_src EXCLUDE REGEX ".*/test_local_handler\\.cpp$")

# 并入 kv_event 全部测试，使其随 test_mmc_test 一起运行并计入覆盖率。
# publisher/runtime 测试需要 msgpack-cxx（header-only），不依赖 ZMQ。
list(APPEND mmc_test_src
        ${CMAKE_CURRENT_SOURCE_DIR}/kv_event/test_mmc_kv_event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/kv_event/test_mmc_kv_event_publisher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/kv_event/test_mmc_kv_event_runtime.cpp
)
if (NOT MMC_ENABLE_KV_EVENTS)
    # OFF 时 kv_event 源未编入 mmc_static，需直接编译；ON 时由 mmc_static 提供，避免重复符号。
    list(APPEND mmc_test_src
            ${PROJECT_MMC_SRC_BASE}/csrc/kv_event/mmc_kv_event.cpp
            ${PROJECT_MMC_SRC_BASE}/csrc/kv_event/mmc_kv_event_publisher.cpp)
endif ()

ADD_UNIT_TEST(test_mmc_test
        FILES ${mmc_test_src} ${TEST_SRC_PATHS}/ut_main.cpp
        INCLUDE_PATHS ${TEST_INCLUDE_PATHS} ${PROJECT_MMC_SRC_BASE}/csrc/kv_event ${MMC_KV_EVENTS_MSGPACK_INCLUDE_DIR} ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include
        LINK_LIBRARIES ${TEST_DEPEND_LIBS}
        BUILD_FLAGS -fpermissive --coverage -DMSGPACK_NO_BOOST
)
add_dependencies(test_mmc_test mock_mf_smem)
set_tests_properties(test_mmc_test PROPERTIES
        ENVIRONMENT "MEMFABRIC_HYBRID_EXTEND_LIB_PATH=${PROJECT_OUTPUT_PATH}/test/mock_mf_smem/lib64"
)
