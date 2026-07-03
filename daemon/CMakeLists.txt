# devmgrd_lib: the daemon's bus-free logic (RequestProcessor + authorities).
# Built unconditionally so its unit tests always run; the devmgrd executable
# itself (main + sdbus adaptor + polkit) is added behind DEVMGR_WITH_SDBUS in
# Task 6.
add_library(devmgrd_lib STATIC
    src/request_processor.cpp)
target_include_directories(devmgrd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgrd_lib PUBLIC devmgr_core)
target_compile_features(devmgrd_lib PUBLIC cxx_std_20)
