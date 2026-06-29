find_package(ftxui CONFIG REQUIRED)

add_executable(devmgr-tui src/main.cpp)
target_link_libraries(devmgr-tui
    PRIVATE devmgr_app devmgr_core devmgr_pal_linux
            ftxui::component ftxui::dom ftxui::screen)
target_compile_features(devmgr-tui PRIVATE cxx_std_20)
