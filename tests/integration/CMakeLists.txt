find_package(GTest CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UMOCKDEV REQUIRED IMPORTED_TARGET umockdev-1.0)   # -> PkgConfig::UMOCKDEV

add_executable(devmgr_integration_tests test_udev_enumerator.cpp test_udev_hotplug_monitor.cpp)
target_link_libraries(devmgr_integration_tests
    PRIVATE devmgr_pal_linux devmgr_core
            PkgConfig::UMOCKDEV
            GTest::gtest GTest::gtest_main)
target_compile_features(devmgr_integration_tests PRIVATE cxx_std_20)

# Run UNDER the preload wrapper so libudev hits the umockdev sandbox, never host /sys.
add_test(NAME devmgr_integration
         COMMAND umockdev-wrapper $<TARGET_FILE:devmgr_integration_tests>)
