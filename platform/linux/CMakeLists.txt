find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)   # -> PkgConfig::UDEV

add_library(devmgr_pal_linux STATIC src/udev_device_enumerator.cpp)
target_include_directories(devmgr_pal_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgr_pal_linux
    PUBLIC devmgr_core
    PRIVATE PkgConfig::UDEV)
target_compile_features(devmgr_pal_linux PUBLIC cxx_std_20)
