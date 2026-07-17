find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_ipc_tests test_ipc_round_trip.cpp)
target_link_libraries(devmgr_ipc_tests
    PRIVATE devmgr_core devmgr_pal_linux GTest::gtest GTest::gtest_main)
# The daemon and the recovery CLI binaries, driven end-to-end over the private
# bus below ($<TARGET_FILE:...> also makes them build dependencies of the test).
target_compile_definitions(devmgr_ipc_tests PRIVATE
    DEVMGRD_BIN="$<TARGET_FILE:devmgrd>"
    DEVMGR_CLI_BIN="$<TARGET_FILE:devmgr>")
target_compile_features(devmgr_ipc_tests PRIVATE cxx_std_20)

# One private session bus for daemon + client; nothing touches the real buses.
add_test(NAME devmgr_ipc COMMAND dbus-run-session -- $<TARGET_FILE:devmgr_ipc_tests>)
