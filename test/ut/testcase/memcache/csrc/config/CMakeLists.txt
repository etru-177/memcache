file(GLOB_RECURSE config_src
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_configuration.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_configuration_url_resolve.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_kv_parser.cpp
)
ADD_UNIT_TEST(test_config
        FILES ${config_src} ${TEST_SRC_PATHS}/ut_main.cpp
        INCLUDE_PATHS ${TEST_INCLUDE_PATHS}
        LINK_LIBRARIES ${TEST_DEPEND_LIBS}
        BUILD_FLAGS -fpermissive --coverage
)
