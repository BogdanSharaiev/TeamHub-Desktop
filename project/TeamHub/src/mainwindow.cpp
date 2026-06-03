#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , activeSidePanel(0)
{
    setWindowTitle("TeamHub");
    resize(1400, 900);
    setMinimumSize(800, 500);

    setupMenuBar();
    setupMainToolBar();
    setupCentralWidget();
    setupBottomDock();
    setupVoipDock();
    setupStatusBar();
    applyTheme();

    setSidePanelPage(0);
    btnFiles->setChecked(true);
    connect(editorTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(editor, &CodeEditor::cursorPositionUpdated, this, &MainWindow::onCursorPositionUpdated);
    connect(editor, &CodeEditor::modifyChanged, this, &MainWindow::onModificationChanged);

    outputPane->appendPlainText("[TeamHub] Ready.");
    updateWindowTitle();

    voiceChat = new VoiceChat(this);

    connect(voiceChat, &VoiceChat::statusChanged, this, &MainWindow::onVoipStatusChanged);
    connect(voiceChat, &VoiceChat::peerConnected, this, &MainWindow::onVoipPeerConnected);
    connect(voiceChat, &VoiceChat::peerDisconnected, this, &MainWindow::onVoipPeerDisconnected);

    connect(voiceChat, &VoiceChat::peersUpdated, this, &MainWindow::onVoipPeersUpdated);
    connect(voiceChat, &VoiceChat::roomsUpdated, this, [this](const QStringList &rooms) {
        voipRoomsList->clear();

        for (const QString &r : rooms) {
            voipRoomsList->addItem(r);
        }

        outputPane->appendPlainText("[VoIP] Rooms updated: " + QString::number(rooms.size()));
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::onVoipStatusChanged(const QString &status)
{
    if (voipStatusLabel)
        voipStatusLabel->setText(status);

    outputPane->appendPlainText("[VoIP] " + status);
}

void MainWindow::onVoipCallClicked()
{
    if (voiceChat->isCallActive()) {
        voiceChat->stopCall();
        voipCallBtn->setText("Start Call");
    } else {
        voiceChat->startCall();
        voipCallBtn->setText("Stop Call");
    }
}

void MainWindow::onVoipPeerConnected(const QString &ip, quint16 port)
{
    if (voipPeersLabel)
        voipPeersLabel->setText(QString("Peer: %1:%2 ✓").arg(ip).arg(port));

    outputPane->appendPlainText(QString("[VoIP] P2P link up: %1:%2").arg(ip).arg(port));
}

void MainWindow::onVoipPeerDisconnected(const QString &ip, quint16 port)
{
    Q_UNUSED(ip)
    Q_UNUSED(port)

    if (voipPeersLabel)
        voipPeersLabel->setText("Peers: none");

    outputPane->appendPlainText("[VoIP] Peer left");
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (editor->isModified()) {
        const auto btn
            = QMessageBox::question(this,
                                    "Unsaved Changes",
                                    "The current file has unsaved changes.\nSave before closing?",
                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                    QMessageBox::Save);

        if (btn == QMessageBox::Save) {
            if (!saveFile()) {
                event->ignore();
                return;
            }
        } else if (btn == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

CodeEditor *MainWindow::createTab(const QString &name)
{
    CodeEditor *ed = new CodeEditor(editorTabs);
    connect(ed, &CodeEditor::cursorPositionUpdated, this, &MainWindow::onCursorPositionUpdated);

    connect(ed, &CodeEditor::modifyChanged, this, &MainWindow::onModificationChanged);

    editorTabs->addTab(ed, name);
    return ed;
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New File", this, &MainWindow::newFile, QKeySequence::New);
    fileMenu->addAction("&Open Folder", this, &MainWindow::openFolder);
    fileMenu->addAction("&Open File...", this, &MainWindow::openFile, QKeySequence::Open);
    fileMenu->addAction("&Save", this, [this] { saveFile(); }, QKeySequence::Save);
    fileMenu->addAction("Save &As...", this, [this] { saveFileAs(); }, QKeySequence::SaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(
        "Close &Tab",
        this,
        [this] { onTabCloseRequested(editorTabs->currentIndex()); },
        QKeySequence("Ctrl+W"));
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu *editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("&Undo", this, [this] { editor->undo(); }, QKeySequence::Undo);
    editMenu->addAction("&Redo", this, [this] { editor->redo(); }, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction("Cu&t", this, [this] { editor->cut(); }, QKeySequence::Cut);
    editMenu->addAction("&Copy", this, [this] { editor->copy(); }, QKeySequence::Copy);
    editMenu->addAction("&Paste", this, [this] { editor->paste(); }, QKeySequence::Paste);
    editMenu
        ->addAction("Select &All", this, [this] { editor->selectAll(); }, QKeySequence::SelectAll);
    editMenu->addSeparator();
    auto *actFind = editMenu->addAction("&Find / Replace...", QKeySequence("Ctrl+F"));
    connect(actFind, &QAction::triggered, this, [this]() {
        CodeEditor *ed = qobject_cast<CodeEditor *>(editorTabs->currentWidget());
        if (ed)
            ed->showSearch();
    });
    auto *actGoto = editMenu->addAction("&Go to Line...", QKeySequence("Ctrl+G"));
    actGoto->setEnabled(false);

    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Toggle &Side Panel",
                        this,
                        &MainWindow::toggleSidePanel,
                        QKeySequence("Ctrl+B"));
    viewMenu->addAction("Toggle &Output Panel",
                        this,
                        &MainWindow::toggleBottomDock,
                        QKeySequence("Ctrl+J"));
    viewMenu->addAction("Toggle &VoIP Panel", this, &MainWindow::toggleVoipDock);
    viewMenu->addSeparator();
    viewMenu->addAction("Zoom &In", this, [this] { editor->zoomIn(); }, QKeySequence::ZoomIn);
    viewMenu->addAction("Zoom &Out", this, [this] { editor->zoomOut(); }, QKeySequence::ZoomOut);
    viewMenu->addAction("Reset &Zoom", this, [this] { editor->resetZoom(); }, QKeySequence("Ctrl+0"));
    viewMenu->addSeparator();
    QMenu *themeMenu = viewMenu->addMenu("&Theme");
    themeMenu->addAction("Dark", this, [this] { editor->setTheme(CodeEditor::Theme::Dark); });
    themeMenu->addAction("Light", this, [this] { editor->setTheme(CodeEditor::Theme::Light); });

    QMenu *gitMenu = menuBar()->addMenu("&Git");
    for (auto *a : {
             gitMenu->addAction("Init Repository"),
             gitMenu->addAction("Clone..."),
         })
        a->setEnabled(false);
    gitMenu->addSeparator();
    for (auto *a : {
             gitMenu->addAction("Pull"),
             gitMenu->addAction("Push"),
             gitMenu->addAction("Commit..."),
         })
        a->setEnabled(false);
    gitMenu->addSeparator();
    for (auto *a : {
             gitMenu->addAction("Branches"),
             gitMenu->addAction("Diff"),
         })
        a->setEnabled(false);

    QMenu *teamMenu = menuBar()->addMenu("&Team");
    teamMenu->addAction("Connect to Server", this, &MainWindow::joinCollab);
    teamMenu->addSeparator();
    teamMenu->addAction("Members")->setEnabled(false);
    teamMenu->addAction("Share Session")->setEnabled(false);
    teamMenu->addAction("Voice Call", this, &MainWindow::toggleVoipDock);
}

void MainWindow::setupMainToolBar()
{
    auto *tb = addToolBar("Main");
    tb->setObjectName("mainToolBar");
    tb->setMovable(false);

    tb->addAction("New", this, &MainWindow::newFile);
    tb->addAction("Open", this, &MainWindow::openFile);
    tb->addAction("Save", this, [this] { saveFile(); });
    tb->addSeparator();

    auto *actRun = tb->addAction("Run", this, &MainWindow::runFile);
    actRun->setToolTip("Run (F5)");
    actRun->setShortcut(QKeySequence("F5"));

    auto *actDebug = tb->addAction("Debug");
    actDebug->setToolTip("Start Debugging (F9)  —  stub");
    actDebug->setShortcut(QKeySequence("F9"));
    actDebug->setEnabled(false);

    tb->addSeparator();

    auto *actCollab = tb->addAction("Collab");
    actCollab->setCheckable(true);
    connect(actCollab, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            bool ok;
            QString room = QInputDialog::getText(this,
                                                 "Join Collaboration",
                                                 "Enter room name:",
                                                 QLineEdit::Normal,
                                                 "",
                                                 &ok);
            if (!ok || room.isEmpty()) {
                qobject_cast<QAction *>(sender())->setChecked(false);
                return;
            }
            startCollab(room);
        } else {
            stopAllCollab();
        }
    });

    auto *actCall = tb->addAction("Call");
    actCall->setToolTip("Toggle Voice");
    connect(actCall, &QAction::triggered, this, &MainWindow::toggleVoipDock);
}

void MainWindow::startCollab(const QString& room)
{
    CodeEditor* ed = qobject_cast<CodeEditor*>(editorTabs->currentWidget());
    if (!ed) return;

    if (collabManagers.contains(ed)) {
        RGAManager* old = collabManagers.take(ed);
        collabTabs.remove(ed);
        markTabAsCollab(ed, false);
        disconnect(ed, nullptr, old, nullptr);
        disconnect(old, nullptr, ed, nullptr);
        old->disconnectFromServer();
        old->deleteLater();
    }

    const int siteId = static_cast<int>(
        QRandomGenerator::global()->bounded(100000u, 999999u));
    auto* mgr = new RGAManager(siteId, this);

    collabManagers[ed] = mgr;
    collabTabs.insert(ed);
    markTabAsCollab(ed, true);

    const QString url  = QString("ws://localhost:8765/%1").arg(room);
    const QString text = ed->text();
    const QString path = ed->getFilePath();

    wireEditorToManager(ed, mgr);

    mgr->connectToServer(url);

    connect(mgr, &RGAManager::connected, this,
            [this, mgr, text, path]() {
                mgr->buildFromText(text);
                mgr->sendInitText(text, path);
                outputPane->appendPlainText("[Collab] Started — you are host");
                if (collabStatusLabel)
                    collabStatusLabel->setText("Hosting collab");
            }, Qt::SingleShotConnection);

    connect(mgr, &RGAManager::errorOccurred, this,
            [this](const QString& err) {
                outputPane->appendPlainText("[Collab] Error: " + err);
            });

    outputPane->appendPlainText("[Collab] Starting room: " + room);
}

void MainWindow::joinCollab()
{
    bool ok = false;
    const QString room = QInputDialog::getText(
                             this, "Join Collaboration",
                             "Enter room name:", QLineEdit::Normal, "", &ok).trimmed();

    if (!ok || room.isEmpty()) return;

    const QString url = QString("ws://localhost:8765/%1").arg(room);
    const int siteId  = static_cast<int>(
        QRandomGenerator::global()->bounded(100000u, 999999u));

    auto* mgr = new RGAManager(siteId, this);

    outputPane->appendPlainText("[Collab] Joining room: " + room);

    connect(mgr, &RGAManager::onInitReceived, this,
            [this, mgr](const QString& text, const QString& filename) {
                const QString tabName = filename.isEmpty() ? "Shared" : filename;

                CodeEditor* ed = createTab(tabName);
                ed->applyRemoteText(text);
                editorTabs->setCurrentWidget(ed);

                collabManagers[ed] = mgr;
                collabTabs.insert(ed);
                markTabAsCollab(ed, true);

                wireEditorToManager(ed, mgr);

                outputPane->appendPlainText("[Collab] Joined: " + tabName);
                if (collabStatusLabel)
                    collabStatusLabel->setText("Joined: " + tabName);
            });

    connect(mgr, &RGAManager::errorOccurred, this,
            [this, mgr](const QString& err) {
                outputPane->appendPlainText("[Collab] Error: " + err);
                if (!collabManagers.values().contains(mgr))
                    mgr->deleteLater();
            });

    mgr->connectToServer(url);
}

void MainWindow::wireEditorToManager(CodeEditor* ed, RGAManager* mgr)
{
    connect(ed, &CodeEditor::localInsert,
            mgr, &RGAManager::localInsert,
            Qt::UniqueConnection);

    connect(ed, &CodeEditor::localDelete,
            mgr, &RGAManager::localRemove,
            Qt::UniqueConnection);

    connect(mgr, &RGAManager::remoteTextChanged,
            ed,
            [ed](const QString& newText)
            {
                ed->applyRemoteText(newText);
            });

    connect(ed, &CodeEditor::cursorPositionUpdated,
            ed,
            [mgr, ed](int, int)
            {
                const int pos =
                    ed->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS);

                mgr->sendCursorPosition(pos);
            });

    connect(mgr, &RGAManager::remoteCursorMoved,
            ed,
            &CodeEditor::updateRemoteCursor,
            Qt::UniqueConnection);

    connect(mgr, &RGAManager::remoteCursorLeft,
            ed,
            &CodeEditor::removeRemoteCursor,
            Qt::UniqueConnection);

    connect(mgr, &RGAManager::usersUpdated,
            this,
            &MainWindow::onCollabUsersUpdated,
            Qt::UniqueConnection);
}

void MainWindow::stopAllCollab()
{
    for (auto it = collabManagers.begin(); it != collabManagers.end(); ++it) {
        CodeEditor* ed  = it.key();
        RGAManager* mgr = it.value();

        disconnect(ed,  nullptr, mgr, nullptr);
        disconnect(mgr, nullptr, ed,  nullptr);
        disconnect(mgr, nullptr, this, nullptr);

        ed->clearRemoteCursors();
        mgr->disconnectFromServer();
        mgr->deleteLater();

        markTabAsCollab(ed, false);
    }

    collabManagers.clear();
    collabTabs.clear();

    if (collabUsersList)  collabUsersList->clear();
    if (collabStatusLabel) collabStatusLabel->setText("Not in collab");

    outputPane->appendPlainText("[Collab] All sessions stopped.");
}

void MainWindow::markTabAsCollab(CodeEditor* ed, bool on)
{
    const int idx = editorTabs->indexOf(ed);
    if (idx < 0) return;

    QString name = QFileInfo(ed->getFilePath()).fileName();
    if (name.isEmpty()) name = "Untitled";

    editorTabs->setTabText(idx, on ? ("⚡ " + name) : name);
}

void MainWindow::onCollabUsersUpdated(QList<int> siteIds)
{
    if (!collabUsersList) return;
    collabUsersList->clear();
    for (int id : siteIds)
        collabUsersList->addItem(QString("User #%1").arg(id));
    if (collabStatusLabel && !siteIds.isEmpty())
        collabStatusLabel->setText(
            QString("Collab — %1 user(s)").arg(siteIds.size()));
}

void MainWindow::onTabCloseRequested(int tabIndex)
{
    if (editorTabs->count() <= 1) return;

    CodeEditor* tabEditor = qobject_cast<CodeEditor*>(
        editorTabs->widget(tabIndex));
    if (!tabEditor) return;

    if (collabTabs.contains(tabEditor)) {
        RGAManager* mgr = collabManagers.take(tabEditor);
        collabTabs.remove(tabEditor);
        if (mgr) {
            disconnect(tabEditor, nullptr, mgr,        nullptr);
            disconnect(mgr,       nullptr, tabEditor,  nullptr);
            disconnect(mgr,       nullptr, this,       nullptr);
            tabEditor->clearRemoteCursors();
            mgr->disconnectFromServer();
            mgr->deleteLater();
        }
        outputPane->appendPlainText("[Collab] Tab closed — session stopped.");
    }

    if (tabEditor->isModified()) {
        const auto btn = QMessageBox::question(
            this, "Unsaved Changes",
            "Save changes before closing this tab?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (btn == QMessageBox::Save) {
            editor = tabEditor;
            if (!saveFile()) return;
        } else if (btn == QMessageBox::Cancel) {
            return;
        }
    }

    editorTabs->removeTab(tabIndex);
}

void MainWindow::setSidePanelPage(int index)
{
    const QStringList titles = {"EXPLORER", "TASKS", "TEAM", "COLLAB"};

    if (index == activeSidePanel && leftPanel->isVisible()) {
        leftPanel->setVisible(false);
        activeSidePanel = -1;
        btnFiles->setChecked(false);
        btnTasks->setChecked(false);
        btnTeam->setChecked(false);
        return;
    }

    activeSidePanel = index;
    leftPanel->setVisible(true);
    leftStack->setCurrentIndex(index);
    leftTitle->setText(titles.value(index, "PANEL"));

    btnFiles->setChecked(index == 0);
    btnTasks->setChecked(index == 1);
    btnTeam->setChecked(index == 2);
}



void MainWindow::setupCentralWidget()
{
    auto *root = new QWidget(this);
    auto *hbox = new QHBoxLayout(root);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);

    setupActivityBar();
    setupLeftPanel();
    setupEditorArea();

    centralSplitter = new QSplitter(Qt::Horizontal, root);
    centralSplitter->setObjectName("centralSplitter");
    centralSplitter->setChildrenCollapsible(false);
    centralSplitter->addWidget(leftPanel);
    centralSplitter->addWidget(editorTabs);
    centralSplitter->setStretchFactor(0, 0);
    centralSplitter->setStretchFactor(1, 1);
    centralSplitter->setSizes({260, 1140});

    hbox->addWidget(activityBar);
    hbox->addWidget(centralSplitter);

    setCentralWidget(root);
}

void MainWindow::setupActivityBar()
{
    activityBar = new QWidget;
    activityBar->setObjectName("activityBar");
    activityBar->setFixedWidth(48);

    auto *vbox = new QVBoxLayout(activityBar);
    vbox->setContentsMargins(0, 8, 0, 8);
    vbox->setSpacing(0);

    auto makeBtn = [](const QString &label, const QString &tip) {
        auto *btn = new QToolButton;
        btn->setText(label);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setFixedSize(48, 48);
        btn->setObjectName("activityBtn");
        return btn;
    };

    btnFiles = makeBtn("Files", "Explorer  (Ctrl+B)");
    btnTasks = makeBtn("Tasks", "Task Manager");
    btnTeam = makeBtn("Team", "Team");

    vbox->addWidget(btnFiles);
    vbox->addWidget(btnTasks);
    vbox->addWidget(btnTeam);
    vbox->addStretch(1);

    auto* btnCollab = makeBtn("Collab", "Collaboration");
    vbox->addWidget(btnCollab);
    connect(btnCollab, &QToolButton::clicked,
            this, [this]{ onActivityButton(3); });

    btnVoip = makeBtn("VoIP", "Voice");
    btnVoip->setObjectName("activityBtnVoip");
    vbox->addWidget(btnVoip);

    connect(btnFiles, &QToolButton::clicked, this, [this] { onActivityButton(0); });
    connect(btnTasks, &QToolButton::clicked, this, [this] { onActivityButton(1); });
    connect(btnTeam, &QToolButton::clicked, this, [this] { onActivityButton(2); });
    connect(btnVoip, &QToolButton::clicked, this, &MainWindow::toggleVoipDock);
}

void MainWindow::setupLeftPanel()
{
    leftPanel = new QWidget;
    leftPanel->setObjectName("leftPanel");
    leftPanel->setMinimumWidth(140);

    auto *vbox = new QVBoxLayout(leftPanel);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    leftTitle = new QLabel("EXPLORER");
    leftTitle->setObjectName("panelTitle");
    leftTitle->setContentsMargins(12, 5, 12, 5);
    vbox->addWidget(leftTitle);

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("panelSeparator");
    vbox->addWidget(sep);

    leftStack = new QStackedWidget;

    fileBrowser = new FileBrowser(this);
    fileBrowser->setRootPath("C:/TeamHub-Desktop/project/TeamHub");
    leftStack->addWidget(fileBrowser);

    taskList = new QListWidget;
    taskList->setObjectName("taskList");
    taskList->addItem("Task Manager");
    leftStack->addWidget(taskList);

    teamList = new QListWidget;
    teamList->setObjectName("teamList");
    teamList->addItem("Team Panel");
    leftStack->addWidget(teamList);

    vbox->addWidget(leftStack, 1);
    auto* collabPane = new QWidget;
    auto* vl = new QVBoxLayout(collabPane);

    collabStatusLabel = new QLabel("Not in collab");
    collabStatusLabel->setObjectName("stubLabel");
    vl->addWidget(collabStatusLabel);

    collabUsersList = new QListWidget;
    collabUsersList->setObjectName("taskList");
    vl->addWidget(collabUsersList, 1);

    btnStopAllCollab = new QPushButton("Stop All Collab");
    btnStopAllCollab->setObjectName("voipBtn");
    connect(btnStopAllCollab, &QPushButton::clicked,
            this, &MainWindow::stopAllCollab);
    vl->addWidget(btnStopAllCollab);

    leftStack->addWidget(collabPane);

    connect(fileBrowser, &FileBrowser::fileDoubleClicked, this, &MainWindow::openFileFromBrowser);
}

void MainWindow::openFileFromBrowser(const QString &path)
{
    for (int i = 0; i < editorTabs->count(); i++) {
        CodeEditor *existingEditor = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (existingEditor && existingEditor->getFilePath() == path) {
            editorTabs->setCurrentIndex(i);
            return;
        }
    }

    CodeEditor *newEditor = new CodeEditor(editorTabs);
    newEditor->loadFile(path);
    connect(newEditor,
            &CodeEditor::cursorPositionUpdated,
            this,
            &MainWindow::onCursorPositionUpdated);
    connect(newEditor, &CodeEditor::modifyChanged, this, &MainWindow::onModificationChanged);

    const QString name = QFileInfo(path).fileName();
    int index = editorTabs->addTab(newEditor, name);
    editorTabs->setCurrentIndex(index);
    currentFilePath = path;
    //statusFile->setText(name);
    updateWindowTitle();
    outputPane->appendPlainText("[TeamHub] Opened: " + path);
}

void MainWindow::setupEditorArea()
{
    editorTabs = new QTabWidget;
    editorTabs->setObjectName("editorTabs");
    editorTabs->setTabsClosable(true);
    editorTabs->setMovable(true);
    editorTabs->setDocumentMode(true);

    editor = new CodeEditor(editorTabs);
    editorTabs->addTab(editor, "Untitled");

    connect(editorTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
}

void MainWindow::setupBottomDock()
{
    bottomDock = new QDockWidget("Panel", this);
    bottomDock->setObjectName("bottomDock");
    bottomDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    bottomDock->setTitleBarWidget(new QWidget);

    bottomTabs = new QTabWidget;
    bottomTabs->setObjectName("bottomTabs");
    bottomTabs->setTabPosition(QTabWidget::South);

    outputPane = new QPlainTextEdit;
    outputPane->setObjectName("outputPane");
    outputPane->setReadOnly(true);
    outputPane->setPlaceholderText("Build and run output will appear here...");
    bottomTabs->addTab(outputPane, "Output");

    terminal = new Terminal(this);
    terminal->setWorkingDirectory(fileBrowser->rootPath());
    terminal->setObjectName("terminal");
    bottomTabs->addTab(terminal, "Terminal");

    gitPane = new QWidget;
    gitPane->setObjectName("gitPane");
    {
        auto *l = new QVBoxLayout(gitPane);
        auto *lbl = new QLabel("Git integration\n\n"
                               "Planned: staged / unstaged changes, commit history, inline diff.");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setWordWrap(true);
        lbl->setObjectName("stubLabel");
        l->addWidget(lbl);
    }
    bottomTabs->addTab(gitPane, "Git");

    bottomDock->setWidget(bottomTabs);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    resizeDocks({bottomDock}, {180}, Qt::Vertical);
}

void MainWindow::setupVoipDock()
{
    voipDock = new QDockWidget("Voice Rooms", this);
    voipDock->setObjectName("voipDock");

    auto *panel = new QWidget;
    panel->setObjectName("voipPanel");

    auto *root = new QVBoxLayout(panel);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    voipStatusLabel = new QLabel("Disconnected");
    voipStatusLabel->setObjectName("stubLabel");
    root->addWidget(voipStatusLabel);

    voipPeersLabel = new QLabel("Peers: none");
    voipPeersLabel->setObjectName("stubLabel");
    root->addWidget(voipPeersLabel);

    auto *roomsHeader = new QLabel("ROOMS");
    roomsHeader->setObjectName("stubLabel");
    root->addWidget(roomsHeader);

    voipRoomsList = new QListWidget;
    voipRoomsList->setObjectName("voipRoomsList");
    root->addWidget(voipRoomsList, 2);

    btnCreateRoom = new QPushButton("Create Room");
    btnCreateRoom->setObjectName("voipBtn");
    root->addWidget(btnCreateRoom);

    auto *usersHeader = new QLabel("USERS");
    usersHeader->setObjectName("stubLabel");
    root->addWidget(usersHeader);

    voipUsersList = new QListWidget;
    voipUsersList->setObjectName("voipUsersList");
    root->addWidget(voipUsersList, 3);

    auto *controls = new QFrame;
    controls->setObjectName("voipControls");

    auto *h = new QHBoxLayout(controls);
    h->setContentsMargins(0, 0, 0, 0);

    btnMuteMic = new QPushButton(u8"\U0001F3A4");
    btnMuteMic->setCheckable(true);

    btnDeafen = new QPushButton(u8"\U0001F3A7");
    btnDeafen->setCheckable(true);

    btnLeave = new QPushButton("Leave");

    for (auto *b : {btnMuteMic, btnDeafen, btnLeave})
        b->setObjectName("voipBtn");

    h->addWidget(btnMuteMic);
    h->addWidget(btnDeafen);
    h->addWidget(btnLeave);

    root->addWidget(controls);

    voipDock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, voipDock);
    voipDock->hide();

    setupVoipConnections();
}

void MainWindow::setupStatusBar()
{
    statusFile = new QLabel("Untitled");
    statusPosition = new QLabel("Ln 1, Col 1");
    statusEncoding = new QLabel("UTF-8");
    statusLanguage = new QLabel("Python");

    for (auto *lbl : {statusFile, statusPosition, statusEncoding, statusLanguage}) {
        lbl->setContentsMargins(8, 0, 8, 0);
        lbl->setObjectName("statusLabel");
    }

    statusBar()->addWidget(statusFile, 1);
    statusBar()->addPermanentWidget(statusPosition);
    statusBar()->addPermanentWidget(statusEncoding);
    statusBar()->addPermanentWidget(statusLanguage);
}

void MainWindow::onTabChanged(int index)
{
    CodeEditor *activeEditor = qobject_cast<CodeEditor *>(editorTabs->widget(index));
    if (!activeEditor)
        return;
    editor = activeEditor;
    QString path = activeEditor->getFilePath();
    currentFilePath = path;
    QString filename;
    if (path.isEmpty()) {
        filename = "Untitled";
        statusFile->setText(filename);
    } else {
        filename = QFileInfo(path).fileName();
    }
    statusFile->setText(filename);
    statusPosition->setText(QString("Ln %1, Col %2")
                                .arg(activeEditor->currentLine() + 1)
                                .arg(activeEditor->currentColumn() + 1));
    updateWindowTitle();
}

void MainWindow::applyTheme()
{
    setStyleSheet(R"(
/* ── Main window ────────────────────────────────────────────── */
QMainWindow { background: #1e1e1e; }

/* ── Menu bar ───────────────────────────────────────────────── */
QMenuBar {
    background: #3c3c3c;
    color: #cccccc;
    border-bottom: 1px solid #252526;
}
QMenuBar::item:selected { background: #505050; }
QMenu {
    background: #252526;
    color: #cccccc;
    border: 1px solid #454545;
}
QMenu::item:selected    { background: #094771; }
QMenu::item:disabled    { color: #555555; }
QMenu::separator {
    height: 1px;
    background: #454545;
    margin: 2px 0;
}

/* ── Toolbar ────────────────────────────────────────────────── */
QToolBar#mainToolBar {
    background: #3c3c3c;
    border-bottom: 1px solid #252526;
    spacing: 2px;
    padding: 2px 4px;
}
QToolBar#mainToolBar QToolButton {
    color: #cccccc;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px 8px;
}
QToolBar#mainToolBar QToolButton:hover   { background: #505050; border-color: #606060; }
QToolBar#mainToolBar QToolButton:pressed { background: #3a3a3a; }
QToolBar#mainToolBar QToolButton:disabled{ color: #555555; }
QToolBar::separator {
    width: 1px;
    background: #555555;
    margin: 4px 4px;
}

/* ── Activity Bar ───────────────────────────────────────────── */
QWidget#activityBar {
    background: #333333;
    border-right: 1px solid #252526;
}
QToolButton#activityBtn,
QToolButton#activityBtnVoip {
    color: #858585;
    background: transparent;
    border: none;
    border-left: 2px solid transparent;
    font-size: 9px;
}
QToolButton#activityBtn:hover,
QToolButton#activityBtnVoip:hover { color: #cccccc; }
QToolButton#activityBtn:checked   { color: #ffffff; border-left-color: #007acc; }
QToolButton#activityBtnVoip:checked { color: #ffffff; border-left-color: #007acc; }

/* ── Left Panel ─────────────────────────────────────────────── */
QWidget#leftPanel {
    background: #252526;
    border-right: 1px solid #3c3c3c;
}
QLabel#panelTitle {
    color: #bbbbbb;
    font-size: 10px;
    font-weight: bold;
    background: #252526;
    letter-spacing: 1px;
}
QFrame#panelSeparator {
    color: #3c3c3c;
    max-height: 1px;
    background: #3c3c3c;
}
QTreeWidget#fileTree {
    background: #252526;
    color: #cccccc;
    border: none;
    outline: 0;
}
QTreeWidget#fileTree::item:hover    { background: #2a2d2e; }
QTreeWidget#fileTree::item:selected { background: #094771; }
QListWidget#taskList,
QListWidget#teamList {
    background: #252526;
    color: #cccccc;
    border: none;
}
QListWidget#taskList::item:hover,
QListWidget#teamList::item:hover    { background: #2a2d2e; }
QListWidget#taskList::item:selected,
QListWidget#teamList::item:selected { background: #094771; }

QTreeView#fileBrowserTree {
    background: #252526;
    color: #cccccc;
    border: none;
    outline: 0;
}
QTreeView#fileBrowserTree::item {
    height: 22px;
    padding-left: 4px;
}
QTreeView#fileBrowserTree::item:hover {
    background: #2a2d2e;
}
QTreeView#fileBrowserTree::item:selected {
    background: #094771;
    color: #ffffff;
}
QTreeView#fileBrowserTree::branch {
    background: #252526;
}
QLineEdit#fileSearch {
    background: #3c3c3c;
    color: #cccccc;
    border: none;
    border-bottom: 1px solid #454545;
    padding: 4px 8px;
    font-size: 12px;
}

/* ── Splitter ───────────────────────────────────────────────── */
QSplitter#centralSplitter::handle {
    background: #3c3c3c;
    width: 1px;
}

/* ── Editor Tabs ────────────────────────────────────────────── */
QTabWidget#editorTabs::pane     { border: none; background: #1e1e1e; }
QTabWidget#editorTabs > QTabBar::tab {
    background: #2d2d2d;
    color: #9d9d9d;
    border: none;
    border-right: 1px solid #252526;
    padding: 6px 14px;
    min-width: 80px;
}
QTabWidget#editorTabs > QTabBar::tab:selected {
    background: #1e1e1e;
    color: #ffffff;
    border-top: 1px solid #007acc;
}
QTabWidget#editorTabs > QTabBar::tab:hover:!selected {
    background: #383838;
    color: #cccccc;
}

/* ── Bottom Dock ────────────────────────────────────────────── */
QDockWidget#bottomDock  { background: #1e1e1e; color: #cccccc; }
QTabWidget#bottomTabs::pane  { border: none; background: #1e1e1e; }
QTabWidget#bottomTabs > QTabBar { background: #252526; }
QTabWidget#bottomTabs > QTabBar::tab {
    background: #252526;
    color: #9d9d9d;
    border: none;
    padding: 4px 12px;
}
QTabWidget#bottomTabs > QTabBar::tab:selected {
    background: #1e1e1e;
    color: #ffffff;
    border-top: 1px solid #007acc;
}
QTabWidget#bottomTabs > QTabBar::tab:hover:!selected {
    color: #cccccc;
    background: #2a2d2e;
}
QPlainTextEdit#outputPane,
QPlainTextEdit#terminalPane {
    background: #1e1e1e;
    color: #d4d4d4;
    border: none;
    font-family: Consolas, "Courier New", monospace;
    font-size: 11px;
}

/* ── VoIP Dock ──────────────────────────────────────────────── */
QDockWidget#voipDock  { color: #cccccc; }
QWidget#voipPanel     { background: #252526; }
QPushButton#voipBtn {
    background: #3c3c3c;
    color: #cccccc;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 5px 10px;
}
QPushButton#voipBtn:hover   { background: #505050; }
QPushButton#voipBtn:pressed { background: #007acc; }
QPushButton#voipBtn:disabled { color: #555555; }
QListWidget#voipRoomsList,
QListWidget#voipUsersList {
    background: #1e1e1e;
    color: #d4d4d4;
    border: 1px solid #333;
}

QFrame#voipControls {
    background: #2a2a2a;
    border-top: 1px solid #444;
}

QPushButton#voipBtn {
    background: #3c3c3c;
    border-radius: 6px;
    padding: 6px;
}
QPushButton#voipBtn:checked {
    background: #007acc;
}
/* ── Shared stub label ──────────────────────────────────────── */
QLabel#stubLabel { color: #555555; font-size: 11px; }

/* ── Search Widget ───────────────────────────────────────── */

QWidget#textSearch {
    background: #2d2d30;
    border: 1px solid #3c3c3c;
    border-radius: 6px;
}

QLineEdit#searchEdit {
    background: #3c3c3c;
    color: #d4d4d4;
    border: 1px solid #505050;
    border-radius: 3px;
    padding: 5px 8px;
    selection-background-color: #094771;
}

QLineEdit#searchEdit:focus {
    border: 1px solid #007acc;
}

QPushButton#searchNavBtn {
    background: transparent;
    color: #cccccc;
    border: none;
    padding: 4px 8px;
}

QPushButton#searchNavBtn:hover {
    background: #3c3c3c;
    border-radius: 3px;
}

QPushButton#searchNavBtn:pressed {
    background: #454545;
}

QPushButton#searchReplaceBtn {
    background: #0e639c;
    color: white;
    border: none;
    border-radius: 3px;
    padding: 5px 12px;
}

QPushButton#searchReplaceBtn:hover {
    background: #1177bb;
}

QPushButton#searchReplaceBtn:pressed {
    background: #0b4f7c;
}

QPushButton#searchReplaceBtn:disabled {
    background: #444444;
    color: #888888;
}

/* ── Status Bar ─────────────────────────────────────────────── */
QStatusBar {
    background: #007acc;
    color: #ffffff;
    font-size: 11px;
}
QStatusBar::item { border: none; }
QLabel#statusLabel { color: #ffffff; padding: 0 4px; }
    )");
}



void MainWindow::updateWindowTitle()
{
    const QString name = currentFilePath.isEmpty() ? "Untitled"
                                                   : QFileInfo(currentFilePath).fileName();
    const QString dirty = editor->isModified() ? " \u25cf" : "";
    setWindowTitle(name + dirty + " \u2014 TeamHub");
}

void MainWindow::onActivityButton(int page)
{
    setSidePanelPage(page);
}

void MainWindow::onCursorPositionUpdated(int line, int index)
{
    statusPosition->setText(QString("Ln %1, Col %2").arg(line + 1).arg(index + 1));
}

void MainWindow::onModificationChanged(bool modified)
{
    const QString name = currentFilePath.isEmpty() ? "Untitled"
                                                   : QFileInfo(currentFilePath).fileName();
    const QString dirty = modified ? " \u25cf" : "";

    setWindowTitle(name + dirty + " \u2014 TeamHub");

    const int idx = editorTabs->currentIndex();
    if (idx >= 0)
        editorTabs->setTabText(idx, name + dirty);
}

void MainWindow::runFile()
{
    CodeEditor *currentEditor = qobject_cast<CodeEditor *>(editorTabs->currentWidget());
    if (!currentEditor)
        return;

    QString path = currentEditor->getFilePath();
    if (path.isEmpty()) {
        outputPane->appendPlainText("[TeamHub] Save file before running.");
        return;
    }

    QString projectDir = fileBrowser->rootPath();
    QString pythonvenv = fileBrowser->findFile("python.exe");
    QString pythonpath = !pythonvenv.isEmpty() ? pythonvenv : "python";
    currentEditor->saveFile(path);
    outputPane->clear();
    bottomTabs->setCurrentWidget(outputPane);
    bottomDock->setVisible(true);

    QProcess *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        outputPane->appendPlainText(QString::fromLocal8Bit(proc->readAllStandardOutput()));
    });

    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, proc](int code, QProcess::ExitStatus) {
                outputPane->appendPlainText(QString("\n[TeamHub] Exit code: %1").arg(code));
                proc->deleteLater();
            });

    proc->start(pythonpath, {path});
}

void MainWindow::newFile()
{
    if (editor->isModified()) {
        const auto btn = QMessageBox::question(this,
                                               "Unsaved Changes",
                                               "Save changes before creating a new file?",
                                               QMessageBox::Save | QMessageBox::Discard
                                                   | QMessageBox::Cancel);
        if (btn == QMessageBox::Save) {
            if (!saveFile())
                return;
        } else if (btn == QMessageBox::Cancel)
            return;
    }

    editor->clear();
    editor->setModified(false);
    currentFilePath.clear();
    statusFile->setText("Untitled");
    editorTabs->setTabText(editorTabs->currentIndex(), "Untitled");
    updateWindowTitle();
}

void MainWindow::clearTabs()
{
    while (editorTabs->count() > 0) {
        QWidget *w = editorTabs->widget(0);
        editorTabs->removeTab(0);
        delete w;
    }
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      "Open File",
                                                      {},
                                                      "Python Files (*.py);;All Files (*)");
    if (path.isEmpty())
        return;

    CodeEditor *newEditor = new CodeEditor(editorTabs);
    newEditor->loadFile(path);
    connect(newEditor,
            &CodeEditor::cursorPositionUpdated,
            this,
            &MainWindow::onCursorPositionUpdated);
    connect(newEditor, &CodeEditor::modifyChanged, this, &MainWindow::onModificationChanged);

    const QString name = QFileInfo(path).fileName();
    int index = editorTabs->addTab(newEditor, name);
    editorTabs->setCurrentIndex(index);

    currentFilePath = path;
    statusFile->setText(name);
    updateWindowTitle();
    outputPane->appendPlainText("[TeamHub] Opened: " + path);
}

void MainWindow::openFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, "Open Folder");
    if (path.isEmpty())
        return;

    fileBrowser->setRootPath(path);
    clearTabs();

    editor = new CodeEditor(editorTabs);
    editorTabs->addTab(editor, "Untitled");

    currentFilePath.clear();
    setWindowTitle(QFileInfo(path).fileName() + " — TeamHub");
    outputPane->appendPlainText("[TeamHub] Opened folder: " + path);

    terminal->setWorkingDirectory(path);
}

bool MainWindow::saveFile()
{
    CodeEditor *activeEditor = qobject_cast<CodeEditor *>(editorTabs->currentWidget());
    if (!activeEditor)
        return false;

    QString path = activeEditor->getFilePath();

    if (path.isEmpty())
        return saveFileAs();

    activeEditor->saveFile(path);

    const QString name = QFileInfo(path).fileName();
    currentFilePath = path;
    statusFile->setText(name);
    editorTabs->setTabText(editorTabs->currentIndex(), name);
    updateWindowTitle();
    outputPane->appendPlainText("[TeamHub] Saved: " + path);
    return true;
}

bool MainWindow::saveFileAs()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save File As",
                                                      {},
                                                      "Python Files (*.py);;All Files (*)");
    if (path.isEmpty())
        return false;

    currentFilePath = path;
    return saveFile();
}

void MainWindow::toggleSidePanel()
{
    if (leftPanel->isVisible()) {
        leftPanel->setVisible(false);
        activeSidePanel = -1;
        btnFiles->setChecked(false);
        btnTasks->setChecked(false);
        btnTeam->setChecked(false);
    } else {
        setSidePanelPage(0);
    }
}

void MainWindow::toggleBottomDock()
{
    bottomDock->setVisible(!bottomDock->isVisible());
}

void MainWindow::toggleVoipDock()
{
    const bool willShow = !voipDock->isVisible();

    voipDock->setVisible(willShow);

    btnVoip->setChecked(willShow);

    if (willShow && !voiceChat->isConnected()) {
        voiceChat->connectToServer("localhost", 9000);

        outputPane->appendPlainText("[VoIP] Connecting...");
    }
}

void MainWindow::onVoipPeersUpdated(const QStringList &ids)
{
    voipUsersList->clear();

    for (const QString &id : ids) {
        voipUsersList->addItem("User ID: " + id);
    }
}

void MainWindow::setupVoipConnections()
{
    connect(btnCreateRoom, &QPushButton::clicked, this, [this]() {
        QString room = QInputDialog::getText(this, "Create Room", "Room name:");
        if (room.isEmpty())
            return;

        addRoom(room);
        joinRoom(room);
    });

    connect(voipRoomsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        joinRoom(item->text());
    });

    connect(btnMuteMic, &QPushButton::toggled, this, [this](bool on) {
        micMuted = on;
        voiceChat->setMicMuted(on);
        outputPane->appendPlainText(on ? "[VoIP] Mic muted" : "[VoIP] Mic unmuted");
    });

    connect(btnDeafen, &QPushButton::toggled, this, [this](bool on) {
        audioMuted = on;
        voiceChat->setAudioMuted(on);
        outputPane->appendPlainText(on ? "[VoIP] Audio muted" : "[VoIP] Audio unmuted");
    });

    connect(btnLeave, &QPushButton::clicked, this, [this]() {
        voiceChat->disconnectFromServer();
        voipUsersList->clear();
        outputPane->appendPlainText("[VoIP] Left room");
    });
}

void MainWindow::addRoom(const QString &room)
{
    bool exists = false;

    for (int i = 0; i < voipRoomsList->count(); i++) {
        if (voipRoomsList->item(i)->text() == room) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        voipRoomsList->addItem(room);
    }
}

void MainWindow::joinRoom(const QString &room)
{
    if (joiningVoiceRoom)
        return;

    if (currentVoiceRoom == room && voiceChat->isConnected()) {
        outputPane->appendPlainText("[VoIP] Already in room: " + room);
        return;
    }

    joiningVoiceRoom = true;
    currentVoiceRoom = room;
    voipUsersList->clear();

    disconnect(voiceChat, &VoiceChat::connectedToServer, this, nullptr);

    connect(
        voiceChat,
        &VoiceChat::connectedToServer,
        this,
        [this]() {
            if (!voiceChat->isCallActive()) {
                voiceChat->startCall();
                outputPane->appendPlainText("[VoIP] Voice stream started");
            }
            joiningVoiceRoom = false;
        },
        Qt::SingleShotConnection);

    voiceChat->setRoom(room);
    outputPane->appendPlainText("[VoIP] Joining room: " + room);

    if (voiceChat->isConnected()) {
        voiceChat->disconnectFromServer();
    }

    QTimer::singleShot(300, this, [this]() { voiceChat->connectToServer("localhost", 9000); });
}