# The Windows read-only backend set (windows-device-inventory spec).
#
# Two kinds of source live here and the split is load-bearing:
#
#   *_mapper.cpp, windows_hotplug_monitor.cpp
#       Platform-NEUTRAL C++ — no <windows.h>, no native property key, no
#       native constant. Every DECISION lives here: which property becomes
#       which model field, which identifier prefix is which bus, when a
#       callback may fire. Compiles on any host, so the unit tests that cover
#       those decisions run on the Linux CI that is this project's main gate.
#
#   cfgmgr_*.cpp, windows_system_info.cpp, platform_backends_windows.cpp
#       Win32. Calls and constants only. Every DEVPKEY_* this backend knows is
#       in these files (spec: native identifiers do not leave the backend), and
#       each numeric constant the neutral half mirrors is static_assert-ed here
#       against the SDK's own macro, so a copy that drifts fails the build.

# D14 — Windows 10 version 1607 is a declared contract, enforced here so an
# earlier target fails loudly at configure time rather than at a runtime
# entry-point lookup. CM_Register_Notification (the hotplug interface this
# backend requires, design D8) does not exist before it, and the alternatives
# were rejected there rather than left as a fallback.
set(DEVMGR_WINDOWS_MIN_NTDDI "0x0A000001")  # NTDDI_WIN10_RS1 == Windows 10 1607
set(DEVMGR_WINDOWS_TARGET_NTDDI "${DEVMGR_WINDOWS_MIN_NTDDI}" CACHE STRING
    "NTDDI_VERSION the Windows backend targets (minimum ${DEVMGR_WINDOWS_MIN_NTDDI}, Windows 10 1607)")
math(EXPR _devmgr_target_ntddi "${DEVMGR_WINDOWS_TARGET_NTDDI}" OUTPUT_FORMAT DECIMAL)
math(EXPR _devmgr_min_ntddi "${DEVMGR_WINDOWS_MIN_NTDDI}" OUTPUT_FORMAT DECIMAL)
if(_devmgr_target_ntddi LESS _devmgr_min_ntddi)
    message(FATAL_ERROR
        "devmgr requires Windows 10 version 1607 or later "
        "(NTDDI_VERSION ${DEVMGR_WINDOWS_MIN_NTDDI}); "
        "DEVMGR_WINDOWS_TARGET_NTDDI is ${DEVMGR_WINDOWS_TARGET_NTDDI}. "
        "Earlier Windows releases are not supported: the device notification "
        "interface this backend requires is unavailable there.")
endif()
# The SDK version a generator was pointed at is the other way an earlier target
# gets requested. Anything below 10.0 cannot supply the required interface.
# Only meaningful when the TARGET is Windows: on any other host
# CMAKE_SYSTEM_VERSION is that system's own version (a kernel release on Linux)
# and comparing it against a Windows version number is nonsense.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SYSTEM_VERSION
   AND CMAKE_SYSTEM_VERSION VERSION_LESS "10.0")
    message(FATAL_ERROR
        "devmgr requires Windows 10 version 1607 or later; CMAKE_SYSTEM_VERSION "
        "is ${CMAKE_SYSTEM_VERSION}. Earlier Windows releases are not supported.")
endif()

add_library(devmgr_pal_windows STATIC
    src/windows_device_mapper.cpp
    src/windows_hotplug_monitor.cpp
    src/cfgmgr_device_enumerator.cpp
    src/cfgmgr_notification_source.cpp
    src/windows_system_info.cpp
    src/platform_backends_windows.cpp)
target_include_directories(devmgr_pal_windows
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(devmgr_pal_windows PUBLIC devmgr_core)
# cfgmgr32: enumeration + notification. setupapi: the DEVPKEY_* definitions and
# the class/installer declarations they come with. version: file-version query
# for driver facts the property store does not answer.
target_link_libraries(devmgr_pal_windows PRIVATE cfgmgr32 setupapi version)
target_compile_features(devmgr_pal_windows PUBLIC cxx_std_20)
target_compile_definitions(devmgr_pal_windows PRIVATE
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=${DEVMGR_WINDOWS_TARGET_NTDDI}
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)
