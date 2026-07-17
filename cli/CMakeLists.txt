# devmgr_cli_lib: the recovery CLI's bus-free logic (verb dispatch, unique-
# prefix resolution, exit-code mapping, output formatting). Built unconditionally
# so its unit tests run even in the nosdbus configuration; the `devmgr`
# executable itself (main + the real DbusPrivilegedChannel) is added behind
# DEVMGR_WITH_SDBUS below, exactly like devmgrd. snapshot-cli spec "Build gating".
add_library(devmgr_cli_lib STATIC src/cli.cpp)
# Repo-root-relative includes for the private src/ header, matching daemon/.
target_include_directories(devmgr_cli_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_cli_lib PUBLIC devmgr_core)
target_compile_features(devmgr_cli_lib PUBLIC cxx_std_20)

if(DEVMGR_WITH_SDBUS)
    add_executable(devmgr src/main.cpp)
    target_link_libraries(devmgr PRIVATE devmgr_cli_lib devmgr_pal_linux)
endif()

add_subdirectory(tests)
