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

# Standalone fwupd double for the design-verification harness (task 3.3). Same
# FakeFwupdDaemon the suite above tests the real provider against, claimed on the
# SYSTEM bus and seeded with a fixed inventory, so the fwupd-present posture is
# reproducible instead of depending on whatever fwupd the host happens to run.
add_executable(devmgr_fake_fwupd
    fake_fwupd_daemon.cpp
    fake_fwupd_main.cpp)
target_link_libraries(devmgr_fake_fwupd
    PRIVATE devmgr_core devmgr_pal_linux SDBusCpp::sdbus-c++)
target_include_directories(devmgr_fake_fwupd PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_features(devmgr_fake_fwupd PRIVATE cxx_std_20)
