find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(CMAKE_AUTOMOC ON)

add_library(devmgr_gui STATIC
    src/device_list_model.cpp
    src/main_window.cpp
    src/qt_ui_dispatcher.cpp)
target_include_directories(devmgr_gui PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_gui
    PUBLIC devmgr_app devmgr_core devmgr_pal_linux Qt6::Widgets)
target_compile_features(devmgr_gui PUBLIC cxx_std_20)

add_executable(devmgr_gui_tests
    tests/gui_test_main.cpp
    tests/test_device_list_model.cpp
    tests/test_main_window.cpp
    tests/test_qt_ui_dispatcher.cpp)
target_include_directories(devmgr_gui_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(devmgr_gui_tests PRIVATE devmgr_gui Qt6::Test GTest::gtest)
gtest_discover_tests(devmgr_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
