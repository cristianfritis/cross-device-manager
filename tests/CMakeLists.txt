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
    unit/test_application_facade_updates.cpp
    unit/test_device_list_vm.cpp
    unit/test_device_detail_vm.cpp
    unit/test_hotplug_service.cpp
    unit/test_status_line_vm.cpp
    unit/test_critical_device_guard.cpp
    unit/test_device_key.cpp
    unit/test_disabled_overlay.cpp
    unit/test_modules_vm.cpp
    unit/test_update_models.cpp
)
target_link_libraries(devmgr_tests PRIVATE devmgr_core devmgr_app GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    # Header-only udev mapping helpers (NO libudev link) — unit-test them directly.
    target_include_directories(devmgr_tests PRIVATE ${CMAKE_SOURCE_DIR}/platform/linux/include)
    # Linux-only concrete PAL units (controller/prober) — these DO link the PAL lib.
    target_sources(devmgr_tests PRIVATE
        unit/test_sysfs_device_controller.cpp
        unit/test_linux_criticality_prober.cpp
        unit/test_request_processor.cpp
        unit/test_dbus_contract.cpp
        unit/test_state_store.cpp
        unit/test_kmod_driver_manager.cpp
        unit/test_kmod_error_taxonomy.cpp
        unit/test_linux_system_info.cpp
        unit/test_enforcement_service.cpp
        unit/test_cab_resolver.cpp
        unit/test_dkms_status_provider.cpp)
    target_link_libraries(devmgr_tests PRIVATE devmgr_pal_linux devmgrd_lib)

    # KmodDriverManagerTest needs real depmod-processed .ko files: libkmod's
    # module lookup reads modules.dep.bin/modules.alias.bin exclusively and
    # has never had a text-index fallback. Build three tiny fake .ko files
    # (host CC + objcopy) once, at configure/build time, and hand the tests
    # the directory they land in via a compile definition.
    find_program(DEVMGR_HOST_CC NAMES cc gcc clang)
    find_program(DEVMGR_HOST_OBJCOPY NAMES objcopy)
    if(NOT DEVMGR_HOST_CC OR NOT DEVMGR_HOST_OBJCOPY)
        message(FATAL_ERROR "KmodDriverManagerTest fixtures need a host C compiler and objcopy")
    endif()
    set(DEVMGR_KMOD_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/kmod_fixtures)
    set(DEVMGR_KMOD_FIXTURE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kmod/generate_fixtures.sh)
    add_custom_command(
        OUTPUT ${DEVMGR_KMOD_FIXTURE_DIR}/helper.ko
               ${DEVMGR_KMOD_FIXTURE_DIR}/dummy.ko
               ${DEVMGR_KMOD_FIXTURE_DIR}/usbhid.ko
        COMMAND ${DEVMGR_KMOD_FIXTURE_SCRIPT} ${DEVMGR_KMOD_FIXTURE_DIR} ${DEVMGR_HOST_CC}
                ${DEVMGR_HOST_OBJCOPY}
        DEPENDS ${DEVMGR_KMOD_FIXTURE_SCRIPT}
        COMMENT "Generating fake .ko fixtures for KmodDriverManagerTest"
        VERBATIM)
    add_custom_target(devmgr_kmod_fixtures DEPENDS
        ${DEVMGR_KMOD_FIXTURE_DIR}/helper.ko
        ${DEVMGR_KMOD_FIXTURE_DIR}/dummy.ko
        ${DEVMGR_KMOD_FIXTURE_DIR}/usbhid.ko)
    add_dependencies(devmgr_tests devmgr_kmod_fixtures)
    target_compile_definitions(devmgr_tests PRIVATE
        DEVMGR_KMOD_FIXTURE_DIR="${DEVMGR_KMOD_FIXTURE_DIR}")
endif()

if(DEVMGR_WITH_SDBUS)
    target_sources(devmgr_tests PRIVATE
        unit/test_fwupd_contract.cpp
        unit/test_fwupd_provider_nodaemon.cpp)
    target_link_libraries(devmgr_tests PRIVATE SDBusCpp::sdbus-c++)  # sdbus::Variant in test bodies
endif()

gtest_discover_tests(devmgr_tests)
