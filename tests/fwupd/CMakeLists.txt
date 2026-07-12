find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_fwupd_tests
    fake_fwupd_daemon.cpp
    test_fwupd_provider_ipc.cpp)
target_link_libraries(devmgr_fwupd_tests
    PRIVATE devmgr_core devmgr_pal_linux SDBusCpp::sdbus-c++ GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_fwupd_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_features(devmgr_fwupd_tests PRIVATE cxx_std_20)

# Private session bus: fake fwupd + provider, nothing touches real buses.
add_test(NAME devmgr_fwupd COMMAND dbus-run-session -- $<TARGET_FILE:devmgr_fwupd_tests>)
