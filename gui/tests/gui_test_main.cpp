#include <gtest/gtest.h>

#include <QApplication>

// Offscreen everywhere (CI containers and interactive host runs alike): these
// tests exercise models/widgets, not a compositor. Must be set before the
// QApplication is constructed.
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
