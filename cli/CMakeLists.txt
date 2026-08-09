# devmgr_cli_lib: the CLI's transport-free logic (verb dispatch, unique-prefix
# resolution, exit-code mapping, output formatting). Built unconditionally so its
# unit tests run even in the nosdbus configuration.
add_library(devmgr_cli_lib STATIC src/cli.cpp)
# Repo-root-relative includes for the private src/ header, matching daemon/.
target_include_directories(devmgr_cli_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_cli_lib PUBLIC devmgr_core)
target_compile_features(devmgr_cli_lib PUBLIC cxx_std_20)

# The `devmgr` binary needs a platform backend, not a transport: its inventory
# verbs read through the device enumerator and never contact a helper. The
# snapshot verbs do need the privileged channel, so the sources implementing
# them are added only when a transport exists — the binary still builds, and
# without one it simply offers no verb that could reach a helper.
# (snapshot-cli spec "Build gating", restated on capability rather than OS.)
add_executable(devmgr src/main.cpp)
target_link_libraries(devmgr PRIVATE devmgr_cli_lib ${DEVMGR_PLATFORM_LIB})
# --version must print and exit 0 without any backend construction
# (release-versioning spec).
add_test(NAME devmgr_cli_version COMMAND devmgr --version)
set_tests_properties(devmgr_cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^devmgr [0-9]+\\.[0-9]+\\.[0-9]+")

install(TARGETS devmgr RUNTIME DESTINATION bin COMPONENT runtime)

add_subdirectory(tests)
