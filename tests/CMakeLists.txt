find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_tests
    unit/test_version.cpp
)
target_link_libraries(devmgr_tests PRIVATE devmgr_core GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(devmgr_tests)
