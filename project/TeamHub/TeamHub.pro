QT += widgets core

CONFIG += c++17

INCLUDEPATH += libs/include
LIBS        += -L$$PWD/libs -lqscintilla2_qt6d

win32 {
    QMAKE_POST_LINK += $$QMAKE_COPY \
        $$shell_path($$PWD/libs/qscintilla2_qt6d.dll) \
        $$shell_path($$OUT_PWD/) $$escape_expand(\\n\\t)
}

SOURCES += \
    src/editor/codeeditor.cpp \
    src/main.cpp \
    src/mainwindow.cpp

HEADERS += \
    src/editor/codeeditor.h \
    src/mainwindow.h

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
