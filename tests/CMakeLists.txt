find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_tests
    unit/test_version.cpp
    unit/test_result.cpp
    unit/test_models.cpp
    unit/test_event_bus.cpp
    unit/test_logging.cpp
    unit/test_task_scheduler.cpp
    unit/test_delayed_scheduler.cpp
    unit/test_fake_pal.cpp
    unit/test_udev_field_mapping.cpp
    unit/test_device_service.cpp
    unit/test_application_facade.cpp
    unit/test_device_list_vm.cpp
    unit/test_device_detail_vm.cpp
    unit/test_hotplug_service.cpp
    unit/test_status_line_vm.cpp
    unit/test_critical_device_guard.cpp
)
target_link_libraries(devmgr_tests PRIVATE devmgr_core devmgr_app GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    # Header-only udev mapping helpers (NO libudev link) — unit-test them directly.
    target_include_directories(devmgr_tests PRIVATE ${CMAKE_SOURCE_DIR}/platform/linux/include)
    # Linux-only concrete PAL units (controller/prober) — these DO link the PAL lib.
    target_sources(devmgr_tests PRIVATE
        unit/test_sysfs_device_controller.cpp
        unit/test_linux_criticality_prober.cpp)
    target_link_libraries(devmgr_tests PRIVATE devmgr_pal_linux)
endif()
gtest_discover_tests(devmgr_tests)
