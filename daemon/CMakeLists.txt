# devmgrd_lib: the daemon's bus-free logic (RequestProcessor + authorities).
# Built unconditionally so its unit tests always run; the devmgrd executable
# itself (main + sdbus adaptor + polkit) is added behind DEVMGR_WITH_SDBUS in
# Task 6.
find_package(nlohmann_json CONFIG REQUIRED)
add_library(devmgrd_lib STATIC
    src/request_processor.cpp
    src/state_store.cpp
    src/enforcement_service.cpp)
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
endif()
