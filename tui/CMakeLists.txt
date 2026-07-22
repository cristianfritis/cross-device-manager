find_package(ftxui CONFIG REQUIRED)

# Pure presentation layer (theme + per-view render functions). Depends only on
# ftxui::dom/screen — NOT ftxui::component — so render functions stay free of
# interactive/stateful widgets and can be exercised against a fixed Screen in
# tests. The interactive shell (Menu/Input/ScreenInteractive) lives in
# tui_app.cpp and links component on top of this.
add_library(devmgr_tui_render STATIC
    src/theme.cpp
    src/views/tab_bar.cpp
    src/views/status_bar.cpp
    src/views/devices_view.cpp
    src/views/modules_view.cpp
    src/views/updates_view.cpp
    src/views/snapshots_view.cpp)
target_include_directories(devmgr_tui_render PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_tui_render PUBLIC ftxui::dom ftxui::screen)
target_compile_features(devmgr_tui_render PUBLIC cxx_std_20)

add_executable(devmgr-tui
    src/main.cpp
    src/tui_app.cpp
    src/ftxui_ui_dispatcher.cpp)
target_include_directories(devmgr-tui PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr-tui
    PRIVATE devmgr_tui_render
            devmgr_app devmgr_core devmgr_pal_linux
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

# Render tests (docs/DESIGN.md §12.1): the pure presentation layer exercised
# against fixed FTXUI screens. Same GTest harness as tests/, but a separate
# binary so it links only devmgr_tui_render (no app/core/PAL wiring).
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)
add_executable(devmgr_tui_tests
    tests/test_theme.cpp
    tests/test_bars_render.cpp
    tests/test_devices_view_render.cpp
    tests/test_modules_view_render.cpp
    tests/test_updates_view_render.cpp
    tests/test_snapshots_view_render.cpp)
target_include_directories(devmgr_tui_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_tui_tests
    PRIVATE devmgr_tui_render GTest::gtest GTest::gtest_main)
gtest_discover_tests(devmgr_tui_tests)
