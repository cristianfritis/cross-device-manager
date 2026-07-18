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

# --version must print and exit 0 without entering the alternate screen
# (release-versioning spec); works with no TTY, which is exactly the point.
add_test(NAME devmgr_tui_version COMMAND devmgr-tui --version)
set_tests_properties(devmgr_tui_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^devmgr-tui [0-9]+\\.[0-9]+\\.[0-9]+")

install(TARGETS devmgr-tui RUNTIME DESTINATION bin COMPONENT runtime)

# Headless launch smoke, mirroring gui's devmgr_gui_selftest: full wiring +
# one enumeration + row-count line, never entering the alternate screen.
add_test(NAME devmgr_tui_selftest COMMAND devmgr-tui --self-test)
set_tests_properties(devmgr_tui_selftest PROPERTIES
    PASS_REGULAR_EXPRESSION "self-test rows: [0-9]+")
