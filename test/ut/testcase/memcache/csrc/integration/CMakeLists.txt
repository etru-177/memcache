file(GLOB_RECURSE three_tier_cache_src
        ${CMAKE_CURRENT_SOURCE_DIR}/test_three_tier_cache.cpp
)
ADD_UNIT_TEST(test_three_tier_cache
        FILES ${three_tier_cache_src} ${TEST_SRC_PATHS}/ut_main.cpp
        INCLUDE_PATHS ${TEST_INCLUDE_PATHS}
        LINK_LIBRARIES ${TEST_DEPEND_LIBS}
        BUILD_FLAGS -fpermissive --coverage
)
