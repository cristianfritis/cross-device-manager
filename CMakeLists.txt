cmake_minimum_required(VERSION 3.21)
project(cross_device_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Auto-detect umockdev so the integration suite turns itself on only where it is safe.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(UMOCKDEV IMPORTED_TARGET umockdev-1.0)   # sets UMOCKDEV_FOUND
endif()
option(DEVMGR_BUILD_INTEGRATION_TESTS "Build umockdev integration tests" ${UMOCKDEV_FOUND})

add_subdirectory(core)
add_subdirectory(app)
if(UNIX AND NOT APPLE)
    add_subdirectory(platform/linux)
    add_subdirectory(daemon)
    add_subdirectory(tui)
    # Qt 6 GUI: auto-ON where system Qt is present, so the tree still
    # configures and builds with no Qt installed. (System packages by design —
    # see the Phase 3 spec; vcpkg Qt is deliberately not used.)
    find_package(Qt6 COMPONENTS Widgets Test QUIET)
    option(DEVMGR_BUILD_GUI "Build the Qt 6 GUI (devmgr-gui + its tests)" ${Qt6_FOUND})
    if(DEVMGR_BUILD_GUI)
        add_subdirectory(gui)
    endif()
endif()
add_subdirectory(tests)
if(DEVMGR_BUILD_INTEGRATION_TESTS)
    add_subdirectory(tests/integration)
endif()
