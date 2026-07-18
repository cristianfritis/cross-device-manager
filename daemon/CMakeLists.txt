# devmgrd_lib: the daemon's bus-free logic (RequestProcessor + authorities).
# Built unconditionally so its unit tests always run; the devmgrd executable
# itself (main + sdbus adaptor + polkit) is added behind DEVMGR_WITH_SDBUS in
# Task 6.
find_package(nlohmann_json CONFIG REQUIRED)
add_library(devmgrd_lib STATIC
    src/request_processor.cpp
    src/state_store.cpp
    src/atomic_file.cpp
    src/entry_json.cpp
    src/snapshot_store.cpp
    src/snapshot_service.cpp
    src/enforcement_service.cpp
    src/sysfs_device_probe.cpp)
target_include_directories(devmgrd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgrd_lib PUBLIC devmgr_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(devmgrd_lib PUBLIC cxx_std_20)

if(DEVMGR_WITH_SDBUS)
    add_executable(devmgrd
        src/main.cpp
        src/manager_adaptor.cpp
        src/polkit_authority.cpp)
    # Repo-root-relative includes for the private src/ headers, matching gui/.
    target_include_directories(devmgrd PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(devmgrd PRIVATE devmgrd_lib devmgr_pal_linux SDBusCpp::sdbus-c++)
    # --version must print and exit 0 with no bus available (release-versioning spec).
    add_test(NAME devmgrd_version COMMAND devmgrd --version)
    set_tests_properties(devmgrd_version PROPERTIES
        PASS_REGULAR_EXPRESSION "^devmgrd [0-9]+\\.[0-9]+\\.[0-9]+")

    # Install rules (packaging-deb spec). "runtime" = binaries, shared by deb
    # and tarball; "system" = FHS-path unit/policy files, deb only (the tarball
    # carries them under packaging/ instead — see packaging/CMakeLists.txt).
    install(TARGETS devmgrd RUNTIME DESTINATION bin COMPONENT runtime)
    install(FILES data/devmgrd.service
            DESTINATION lib/systemd/system COMPONENT system)
    install(FILES data/org.devmgr.Manager1.service
            DESTINATION share/dbus-1/system-services COMPONENT system)
    install(FILES data/org.devmgr.Manager1.conf
            DESTINATION share/dbus-1/system.d COMPONENT system)
    install(FILES data/org.devmgr.policy
            DESTINATION share/polkit-1/actions COMPONENT system)
endif()
