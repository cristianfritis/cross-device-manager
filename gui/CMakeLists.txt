find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(CMAKE_AUTOMOC ON)

add_library(devmgr_gui STATIC
    src/device_list_model.cpp
    src/gui_app.cpp
    src/main_window.cpp
    src/module_list_model.cpp
    src/qt_ui_dispatcher.cpp
    src/update_list_model.cpp)
target_include_directories(devmgr_gui PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_gui
    PUBLIC devmgr_app devmgr_core devmgr_pal_linux Qt6::Widgets)
target_compile_features(devmgr_gui PUBLIC cxx_std_20)

add_executable(devmgr_gui_tests
    tests/gui_test_main.cpp
    tests/test_device_list_model.cpp
    tests/test_main_window.cpp
    tests/test_module_list_model.cpp
    tests/test_qt_ui_dispatcher.cpp
    tests/test_update_list_model.cpp)
target_include_directories(devmgr_gui_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(devmgr_gui_tests PRIVATE devmgr_gui Qt6::Test GTest::gtest)
gtest_discover_tests(devmgr_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(devmgr-gui src/main.cpp)
target_link_libraries(devmgr-gui PRIVATE devmgr_gui)

# Offscreen launch smoke: full wiring (real PAL) + one enumeration + rebuild.
# In containers the netlink hotplug start may fail — the degrade path still
# exits 0; the smoke asserts wiring, not host capabilities.
add_test(NAME devmgr_gui_selftest COMMAND devmgr-gui --self-test)
set_tests_properties(devmgr_gui_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
