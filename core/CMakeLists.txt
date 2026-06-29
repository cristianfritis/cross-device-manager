find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(devmgr_core
    src/version.cpp
)
target_include_directories(devmgr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgr_core
    PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json tl::expected Threads::Threads)
target_compile_features(devmgr_core PUBLIC cxx_std_20)
