file(GLOB_RECURSE client_src
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_meta_net_client.cpp
)
ADD_UNIT_TEST(test_meta_net_client
        FILES ${client_src} ${TEST_SRC_PATHS}/ut_main.cpp
        INCLUDE_PATHS ${TEST_INCLUDE_PATHS}
        LINK_LIBRARIES ${TEST_DEPEND_LIBS}
        BUILD_FLAGS -fpermissive --coverage
)

# Client metric UT — percentile + sampler + manager + store
file(GLOB_RECURSE client_metric_src
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_percentile.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_bandwidth_sampler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_client_metric_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_client_metric_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_bandwidth_collector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_ubs_io_collector.cpp
)
ADD_UNIT_TEST(test_client_metric
        FILES ${client_metric_src} ${TEST_SRC_PATHS}/ut_main.cpp
        INCLUDE_PATHS ${TEST_INCLUDE_PATHS}
        LINK_LIBRARIES ${TEST_DEPEND_LIBS}
        BUILD_FLAGS -fpermissive --coverage
)
