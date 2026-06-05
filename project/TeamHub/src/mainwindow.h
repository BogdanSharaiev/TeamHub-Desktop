#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>

#include "collab/collabsession.h"
#include "editor/codeeditor.h"
#include "filebrowser/filebrowser.h"
#include "terminal/terminal.h"
#include "voicechat/voicechat.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    QString currentVoiceRoom;
    bool joiningVoiceRoom = false;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    //Activity Bar
    QWidget *activityBar;
    QToolButton *btnFiles;
    QToolButton *btnTasks;
    QToolButton *btnTeam;
    QToolButton *btnVoip;
    //Left Sidebar
    QWidget *leftPanel;
    QLabel *leftTitle;
    QStackedWidget *leftStack;
    FileBrowser *fileBrowser;
    QListWidget *taskList;
    QListWidget *teamList;

    QSplitter *centralSplitter;

    //Editor Area
    QTabWidget *editorTabs;
    CodeEditor *editor;

    //Bottom Dock
    QDockWidget *bottomDock;
    QTabWidget *bottomTabs;
    QPlainTextEdit *outputPane;
    QPlainTextEdit *terminalPane;
    Terminal *terminal;
    QWidget *gitPane;
    QDockWidget *voipDock;

    //Status Bar
    QLabel *statusFile;
    QLabel *statusPosition;
    QLabel *statusEncoding;
    QLabel *statusLanguage;

    //State
    int activeSidePanel;
    QString currentFilePath;

    // Collaboration
    CollabSession *session = nullptr;
    QListWidget *collabUsersList = nullptr;
    QLabel *collabStatusLabel = nullptr;
    QPushButton *btnStopAllCollab = nullptr;
    QPushButton *btnStartCollab = nullptr;
    QPushButton *btnJoinCollab = nullptr;
    QWidget *collabNoSessionPane = nullptr;
    QWidget *collabInSessionPane = nullptr;
    QMap<int, QString> peerFiles;
    QList<int> peerSiteIds;
    QString currentCollabFile;

    // Voice
    VoiceChat *voiceChat;
    QLabel *voipStatusLabel;
    QLabel *voipPeersLabel;
    QPushButton *voipCallBtn;
    QLineEdit *voipRoomEdit;
    QPushButton *voipJoinRoomBtn;
    QListWidget *voipUsersList;
    QListWidget *voipRoomsList;
    QPushButton *btnMuteMic;
    QPushButton *btnDeafen;
    QPushButton *btnLeave;
    QPushButton *btnCreateRoom;
    bool micMuted = false;
    bool audioMuted = false;

    //Setup UI
    void setupMenuBar();
    void setupMainToolBar();
    void setupCentralWidget();
    void setupActivityBar();
    void setupLeftPanel();
    void setupEditorArea();
    void setupBottomDock();
    void setupVoipDock();
    void setupVoipConnections();
    void setupStatusBar();
    void applyTheme();

    void setSidePanelPage(int index);
    void updateWindowTitle();

    void clearTabs();
    CodeEditor *createTab(const QString &name);

    void joinRoom(const QString &room);
    void addRoom(const QString &room);

    // Session helpers
    void stopAllCollab();
    void markTabAsCollab(CodeEditor *ed, bool on);
    void wireEditorToManager(CodeEditor *ed, RGAManager *mgr);
    void wireEditorToSession(CodeEditor *ed, const QString &relPath);

    QString toSessionKey(const QString &editorPath) const;

    void onCollabUsersUpdated(QList<int> siteIds);
    void onRemoteFileFocusChanged(int siteId, const QString &file);
    void refreshCollabUsersList();

private slots:
    void onActivityButton(int page);
    void onCursorPositionUpdated(int line, int index);
    void onModificationChanged(bool modified);
    void onTabCloseRequested(int tabIndex);
    void onTabChanged(int index);
    void openFileFromBrowser(const QString &path);

    void newFile();
    void openFile();
    void openFolder();
    void runFile();
    bool saveFile();
    bool saveFileAs();

    void toggleSidePanel();
    void toggleBottomDock();
    void toggleVoipDock();

    void startCollab(const QString &room,
                     CollabSession::Mode mode = CollabSession::Mode::ReadWrite,
                     const QStringList &selectedFiles = {});
    void joinCollab();
    bool showStartCollabDialog();

    void onSessionProjectInit(int hostSiteId, const QStringList &files);
    void onSessionRunOutput(const QString &text);
    void onSessionFileCreated(const QString &relPath);
    void onSessionFileDeleted(const QString &relPath);
    void onSessionFileRenamed(const QString &oldPath, const QString &newPath);

    void onVoipCallClicked();
    void onVoipStatusChanged(const QString &status);
    void onVoipPeerConnected(const QString &ip, quint16 port);
    void onVoipPeerDisconnected(const QString &ip, quint16 port);
    void onVoipPeersUpdated(const QStringList &ids);

    void onCollabUserContextMenu(const QPoint &pos);
};

#endif // MAINWINDOW_H
