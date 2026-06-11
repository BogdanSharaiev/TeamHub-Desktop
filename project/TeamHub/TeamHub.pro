QT += widgets core websockets multimedia network
CONFIG += c++17

INCLUDEPATH += libs/include

LIBS += -L$$PWD/libs -lqscintilla2_qt6d -lopus -lgit2

win32 {
    QMAKE_POST_LINK += $$QMAKE_COPY \
        $$shell_path($$PWD/libs/qscintilla2_qt6d.dll) \
        $$shell_path($$OUT_PWD/) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY \
        $$shell_path($$PWD/libs/libgit2.dll) \
        $$shell_path($$OUT_PWD/) $$escape_expand(\\n\\t)
}

SOURCES += \
    src/collab/collabsession.cpp \
    src/collab/sessionreportdialog.cpp \
    src/debug/debugadapter.cpp \
    src/editor/codeeditor.cpp \
    src/editor/textsearch.cpp \
    src/filebrowser/filebrowser.cpp \
    src/git/gitlogdialog.cpp \
    src/git/gitmanager.cpp \
    src/git/gitpanel.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/rga/rgamanager.cpp \
    src/rga/rgaseq.cpp \
    src/terminal/terminal.cpp \
    src/voicechat/voicechat.cpp

HEADERS += \
    src/collab/collabsession.h \
    src/collab/sessionreport.h \
    src/collab/sessionreportdialog.h \
    src/debug/debugadapter.h \
    src/editor/codeeditor.h \
    src/editor/textsearch.h \
    src/filebrowser/filebrowser.h \
    src/git/gitlogdialog.h \
    src/git/gitmanager.h \
    src/git/gitpanel.h \
    src/mainwindow.h \
    src/rga/rganode.h \
    src/rga/rgamanager.h \
    src/rga/rgaseq.h \
    src/terminal/terminal.h \
    src/voicechat/voicechat.h

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target