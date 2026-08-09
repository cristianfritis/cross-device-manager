cmake_minimum_required(VERSION 3.21)
# Single-source version (release-versioning spec): this project() call is the
# only place the semver lives; everything else reads the generated version.hpp.
project(devmgr VERSION 0.6.0 LANGUAGES CXX)

# Prerelease suffix appended to the semver ("" for a final release).
set(DEVMGR_PRERELEASE "beta.2" CACHE STRING "Prerelease suffix (e.g. beta.1); empty for a final release")
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

# Which platform backend set is compiled and linked (platform-portability spec:
# "The build gates targets on capability, not on operating system"). Exactly one
# definition of pal::PlatformBackends::create() ends up in any binary, and this
# is what chooses it. Auto-detected from the host and overridable:
#   linux    build platform/linux
#   windows  build platform/windows
#   none     build no platform backend at all — core, app and their unit tests
#            configure and build, and no user-facing program target is defined.
#            This is what an unsupported host did implicitly before; it is now
#            stated, and it is the fastest way to check that nothing above the
#            PAL has grown a platform dependency.
if(WIN32)
    set(_devmgr_platform_default "windows")
elseif(UNIX AND NOT APPLE)
    set(_devmgr_platform_default "linux")
else()
    set(_devmgr_platform_default "none")
endif()
set(DEVMGR_PLATFORM "${_devmgr_platform_default}" CACHE STRING
    "Platform backend set to build: linux | windows | none")
set_property(CACHE DEVMGR_PLATFORM PROPERTY STRINGS linux windows none)
if(NOT DEVMGR_PLATFORM MATCHES "^(linux|windows|none)$")
    message(FATAL_ERROR "DEVMGR_PLATFORM must be linux, windows or none (got '${DEVMGR_PLATFORM}')")
endif()

add_subdirectory(core)
add_subdirectory(app)

if(DEVMGR_PLATFORM STREQUAL "linux")
    # devmgrd + DbusPrivilegedChannel: auto-ON where sdbus-c++ >= 2 is present.
    # v2 API only (v2.0 was a breaking rewrite): host = portage 2.3.1, Docker =
    # pinned v2.3.1 source build — see the Phase 4 spec. The tree must still
    # configure and build with no sdbus-c++ installed. Detected BEFORE the
    # daemon subdirectory so its gated devmgrd target sees the option.
    # A STATIC sdbus-c++ (the packaged-build container) exports
    # PkgConfig::Systemd in its link interface but its config file does not
    # recreate the target — provide it first. No-op where libsystemd is absent
    # (e.g. the OpenRC dev host, whose sdbus-c++ is shared).
    # Linux-only: the privileged-channel transport is a Linux concept here, and
    # no other platform ships a helper for it to talk to.
    if(PkgConfig_FOUND AND NOT TARGET PkgConfig::Systemd)
        pkg_check_modules(Systemd QUIET IMPORTED_TARGET libsystemd)
    endif()
    find_package(sdbus-c++ 2 QUIET)
    option(DEVMGR_WITH_SDBUS "Build devmgrd + the D-Bus privileged channel" ${sdbus-c++_FOUND})
    add_subdirectory(platform/linux)
    # The target that defines pal::PlatformBackends::create(). Every user-facing
    # program links this one name, so no program's CMakeLists names a platform.
    set(DEVMGR_PLATFORM_LIB devmgr_pal_linux)
elseif(DEVMGR_PLATFORM STREQUAL "windows")
    add_subdirectory(platform/windows)
    set(DEVMGR_PLATFORM_LIB devmgr_pal_windows)
endif()

# True once a platform backend exists, i.e. some target defines
# pal::PlatformBackends::create(). Every user-facing program needs one; nothing
# below tests the operating system.
if(DEVMGR_PLATFORM_LIB)
    set(DEVMGR_HAVE_PLATFORM_BACKEND ON)
else()
    set(DEVMGR_HAVE_PLATFORM_BACKEND OFF)
endif()

# Linux-only surfaces: the daemon and its packaging are built around polkit, a
# system service manager and distro packages; the TUI's Windows console
# behaviour has not been through the DESIGN.md parity rules yet.
if(DEVMGR_PLATFORM STREQUAL "linux")
    add_subdirectory(daemon)
    add_subdirectory(tui)
endif()

# Recovery + inventory CLI: gated on a platform backend existing, NOT on the
# operating system. Its inventory verbs need only the device enumerator, so it
# is useful on any platform that has one.
if(DEVMGR_HAVE_PLATFORM_BACKEND)
    add_subdirectory(cli)
endif()

# Qt 6 GUI: auto-ON where Qt is present, so the tree still configures and builds
# with no Qt installed. No operating-system condition — the GUI's requirement is
# a toolkit and a platform backend, both of which are tested for directly.
# (Linux consumes system Qt by design — see the Phase 3 spec; vcpkg Qt is
# deliberately not used there.)
find_package(Qt6 COMPONENTS Widgets Test QUIET)
option(DEVMGR_BUILD_GUI "Build the Qt 6 GUI (devmgr-gui + its tests)" ${Qt6_FOUND})
if(DEVMGR_BUILD_GUI AND DEVMGR_HAVE_PLATFORM_BACKEND)
    add_subdirectory(gui)
endif()

if(DEVMGR_PLATFORM STREQUAL "linux")
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
