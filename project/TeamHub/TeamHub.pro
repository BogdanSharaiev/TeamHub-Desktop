QT += widgets core websockets multimedia
CONFIG += c++17

INCLUDEPATH += libs/include

LIBS += -L$$PWD/libs -lqscintilla2_qt6d -lopus

win32 {
    QMAKE_POST_LINK += $$QMAKE_COPY \
        $$shell_path($$PWD/libs/qscintilla2_qt6d.dll) \
        $$shell_path($$OUT_PWD/) $$escape_expand(\\n\\t)
}

SOURCES += \
    src/collab/collabsession.cpp \
    src/editor/codeeditor.cpp \
    src/editor/textsearch.cpp \
    src/filebrowser/filebrowser.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/rga/rgamanager.cpp \
    src/rga/rgaseq.cpp \
    src/terminal/terminal.cpp \
    src/voicechat/voicechat.cpp

HEADERS += \
    src/collab/collabsession.h \
    src/editor/codeeditor.h \
    src/editor/textsearch.h \
    src/filebrowser/filebrowser.h \
    src/mainwindow.h \
    src/rga/rganode.h \
    src/rga/rgamanager.h \
    src/rga/rgaseq.h \
    src/terminal/terminal.h \
    src/voicechat/voicechat.h

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target