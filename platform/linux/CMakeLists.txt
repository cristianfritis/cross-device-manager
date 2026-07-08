find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)   # -> PkgConfig::UDEV
pkg_check_modules(KMOD REQUIRED IMPORTED_TARGET libkmod)   # -> PkgConfig::KMOD

add_library(devmgr_pal_linux STATIC
    src/udev_device_enumerator.cpp
    src/udev_device_mapper.cpp
    src/sysfs_device_controller.cpp
    src/linux_criticality_prober.cpp
    src/udev_hotplug_monitor.cpp
    src/kmod_driver_manager.cpp
    src/linux_system_info.cpp)
target_include_directories(devmgr_pal_linux
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(devmgr_pal_linux
    PUBLIC devmgr_core
    PRIVATE PkgConfig::UDEV PkgConfig::KMOD)
target_compile_features(devmgr_pal_linux PUBLIC cxx_std_20)

if(DEVMGR_WITH_SDBUS)
    target_sources(devmgr_pal_linux PRIVATE src/dbus_privileged_channel.cpp)
    target_link_libraries(devmgr_pal_linux PRIVATE SDBusCpp::sdbus-c++)
    # Lets composition roots (tui/gui) wire the channel only when it exists.
    target_compile_definitions(devmgr_pal_linux PUBLIC DEVMGR_HAS_SDBUS)
endif()
