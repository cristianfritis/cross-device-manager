add_library(devmgr_app STATIC
    src/device_service.cpp
    src/application_facade.cpp
    src/disabled_overlay.cpp
    src/modules_vm.cpp
    src/device_list_vm.cpp
    src/device_detail_vm.cpp
    src/hotplug_service.cpp
    src/status_line_vm.cpp
    src/updates_vm.cpp)
target_include_directories(devmgr_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgr_app PUBLIC devmgr_core)   # NO ftxui, NO libudev
target_compile_features(devmgr_app PUBLIC cxx_std_20)
