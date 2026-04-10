QT += testlib widgets

CONFIG += c++17 testcase
CONFIG -= app_bundle

TARGET   = test_codeeditor
TEMPLATE = app

INCLUDEPATH += \
    $$PWD/../../libs/include \
    $$PWD/../../src

LIBS += -L$$PWD/../../libs -lqscintilla2_qt6d

win32 {
    QMAKE_POST_LINK += $$QMAKE_COPY \
        $$shell_path($$PWD/../../libs/qscintilla2_qt6d.dll) \
        $$shell_path($$OUT_PWD/) $$escape_expand(\\n\\t)
}

SOURCES += \
    test_codeeditor.cpp \
    $$PWD/../../src/editor/codeeditor.cpp

HEADERS += \
    $$PWD/../../src/editor/codeeditor.h
