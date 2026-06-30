find_package(ftxui CONFIG REQUIRED)

add_executable(devmgr-tui
    src/main.cpp
    src/tui_app.cpp
    src/ftxui_ui_dispatcher.cpp)
target_include_directories(devmgr-tui PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr-tui
    PRIVATE devmgr_app devmgr_core devmgr_pal_linux
            ftxui::component ftxui::dom ftxui::screen)
target_compile_features(devmgr-tui PRIVATE cxx_std_20)
