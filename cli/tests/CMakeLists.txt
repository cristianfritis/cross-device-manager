find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Unit tests for the bus-free CLI logic (FakeChannel, no D-Bus). Built in every
# configuration — including nosdbus, where the `devmgr` binary is absent but its
# logic still gets covered. The real-binary end-to-end (daemon-down exit 4,
# round-trip over a private bus) lives in tests/ipc, next to the daemon harness.
add_executable(devmgr_cli_tests test_cli.cpp)
target_link_libraries(devmgr_cli_tests PRIVATE devmgr_cli_lib GTest::gtest GTest::gtest_main)
target_compile_features(devmgr_cli_tests PRIVATE cxx_std_20)
gtest_discover_tests(devmgr_cli_tests)
