cmake_minimum_required(VERSION 3.21)
# Single-source version (release-versioning spec): this project() call is the
# only place the semver lives; everything else reads the generated version.hpp.
project(devmgr VERSION 0.5.0 LANGUAGES CXX)

# Prerelease suffix appended to the semver ("" for a final release).
set(DEVMGR_PRERELEASE "beta.1" CACHE STRING "Prerelease suffix (e.g. beta.1); empty for a final release")
if(DEVMGR_PRERELEASE)
    set(DEVMGR_FULL_VERSION "${PROJECT_VERSION}-${DEVMGR_PRERELEASE}")
else()
    set(DEVMGR_FULL_VERSION "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Packaged-artifact mode (packaging-deb spec): validated below to require the
# full stack (daemon + GUI) and a static sdbus-c++, so the deb never grows a
# dependency on the distro's incompatible 1.x package. Dev builds (OFF) keep
# dynamic linking and behave exactly as before.
option(DEVMGR_PACKAGED_BUILD "Configure for packaged artifacts (static sdbus-c++, CPack)" OFF)

# Auto-detect umockdev so the integration suite turns itself on only where it is safe.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(UMOCKDEV IMPORTED_TARGET umockdev-1.0)   # sets UMOCKDEV_FOUND
endif()
option(DEVMGR_BUILD_INTEGRATION_TESTS "Build umockdev integration tests" ${UMOCKDEV_FOUND})

add_subdirectory(core)
add_subdirectory(app)
if(UNIX AND NOT APPLE)
    # devmgrd + DbusPrivilegedChannel: auto-ON where sdbus-c++ >= 2 is present.
    # v2 API only (v2.0 was a breaking rewrite): host = portage 2.3.1, Docker =
    # pinned v2.3.1 source build — see the Phase 4 spec. The tree must still
    # configure and build with no sdbus-c++ installed. Detected BEFORE the
    # daemon subdirectory so its gated devmgrd target sees the option.
    # A STATIC sdbus-c++ (the packaged-build container) exports
    # PkgConfig::Systemd in its link interface but its config file does not
    # recreate the target — provide it first. No-op where libsystemd is absent
    # (e.g. the OpenRC dev host, whose sdbus-c++ is shared).
    if(PkgConfig_FOUND AND NOT TARGET PkgConfig::Systemd)
        pkg_check_modules(Systemd QUIET IMPORTED_TARGET libsystemd)
    endif()
    find_package(sdbus-c++ 2 QUIET)
    option(DEVMGR_WITH_SDBUS "Build devmgrd + the D-Bus privileged channel" ${sdbus-c++_FOUND})
    add_subdirectory(platform/linux)
    add_subdirectory(daemon)
    # Recovery CLI: bus-free logic builds always (its unit tests run even in the
    # nosdbus config); the devmgr binary is gated on DEVMGR_WITH_SDBUS inside.
    add_subdirectory(cli)
    add_subdirectory(tui)
    # Qt 6 GUI: auto-ON where system Qt is present, so the tree still
    # configures and builds with no Qt installed. (System packages by design —
    # see the Phase 3 spec; vcpkg Qt is deliberately not used.)
    find_package(Qt6 COMPONENTS Widgets Test QUIET)
    option(DEVMGR_BUILD_GUI "Build the Qt 6 GUI (devmgr-gui + its tests)" ${Qt6_FOUND})
    if(DEVMGR_BUILD_GUI)
        add_subdirectory(gui)
    endif()
    if(DEVMGR_PACKAGED_BUILD)
        if(NOT DEVMGR_WITH_SDBUS OR NOT DEVMGR_BUILD_GUI)
            message(FATAL_ERROR "DEVMGR_PACKAGED_BUILD needs the full stack: "
                                "sdbus-c++ 2.x (found: ${DEVMGR_WITH_SDBUS}) and "
                                "Qt6 (found: ${DEVMGR_BUILD_GUI})")
        endif()
        # A shared sdbus-c++ would make the deb depend on a library Ubuntu only
        # ships as an incompatible 1.x — refuse at configure time.
        get_target_property(_sdbus_type SDBusCpp::sdbus-c++ TYPE)
        get_target_property(_sdbus_loc SDBusCpp::sdbus-c++ LOCATION)
        if(_sdbus_type STREQUAL "SHARED_LIBRARY" OR _sdbus_loc MATCHES "\\.so")
            message(FATAL_ERROR "DEVMGR_PACKAGED_BUILD requires a static sdbus-c++ "
                                "(found ${_sdbus_loc}); build sdbus-c++ with "
                                "-DBUILD_SHARED_LIBS=OFF")
        endif()
    endif()
    add_subdirectory(packaging)
endif()
add_subdirectory(tests)
if(DEVMGR_WITH_SDBUS)
    add_subdirectory(tests/ipc)
    add_subdirectory(tests/fwupd)
    add_subdirectory(tests/smoke)
endif()
if(DEVMGR_BUILD_INTEGRATION_TESTS)
    add_subdirectory(tests/integration)
endif()
