file(GLOB_RECURSE client_src
        ${CMAKE_CURRENT_SOURCE_DIR}/test_mmc_meta_net_client.cpp
)
ADD_UNIT_TEST(test_meta_net_client
        FILES ${client_src} ${TEST_SRC_PATHS}/ut_main.cpp
        INCLUDE_PATHS ${TEST_INCLUDE_PATHS}
        LINK_LIBRARIES ${TEST_DEPEND_LIBS}
        BUILD_FLAGS -fpermissive --coverage
)
