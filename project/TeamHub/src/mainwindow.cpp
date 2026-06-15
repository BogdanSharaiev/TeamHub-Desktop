#include "mainwindow.h"

#include "auth/authdialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , activeSidePanel(0)
{
    setWindowTitle("TeamHub");
    resize(1400, 900);
    setMinimumSize(800, 500);

    voiceChat = new VoiceChat(this);

    setupMenuBar();
    setupMainToolBar();
    setupCentralWidget();
    setupBottomDock();
    setupVoipDock();
    setupStatusBar();
    applyTheme();

    if (gitPanel_ && !fileBrowser->rootPath().isEmpty())
        gitPanel_->setRepoPath(fileBrowser->rootPath());

    setSidePanelPage(0);
    btnFiles->setChecked(true);
    connect(editorTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(editor, &CodeEditor::cursorPositionUpdated, this, &MainWindow::onCursorPositionUpdated);
    connect(editor, &CodeEditor::modifyChanged, this, &MainWindow::onModificationChanged);

    outputPane->appendPlainText("[TeamHub] Ready.");
    updateWindowTitle();
    setupAuthManager();

    connect(voiceChat, &VoiceChat::statusChanged, this, &MainWindow::onVoipStatusChanged);
    connect(voiceChat, &VoiceChat::peerConnected, this, &MainWindow::onVoipPeerConnected);
    connect(voiceChat, &VoiceChat::peerDisconnected, this, &MainWindow::onVoipPeerDisconnected);

    connect(voiceChat, &VoiceChat::peersUpdated, this, &MainWindow::onVoipPeersUpdated);
    connect(voiceChat,
            &VoiceChat::roomsUpdated,
            this,
            [this](const QMap<QString, QStringList> &roomUsers) {
                QSet<QString> expanded;
                for (int i = 0; i < voipTree->topLevelItemCount(); i++) {
                    auto *it = voipTree->topLevelItem(i);
                    if (it->isExpanded())
                        expanded.insert(it->data(0, Qt::UserRole).toString());
                }

                voipTree->clear();

                for (auto it = roomUsers.cbegin(); it != roomUsers.cend(); ++it) {
                    const QString &roomName = it.key();
                    const QStringList &userIds = it.value();

                    auto *roomItem = new QTreeWidgetItem(voipTree);
                    roomItem->setText(0, u8"\U0001F50A " + roomName);
                    roomItem->setData(0, Qt::UserRole, roomName);

                    const bool isCurrent = (roomName == currentVoiceRoom);
                    QFont f = roomItem->font(0);
                    f.setBold(isCurrent);
                    roomItem->setFont(0, f);
                    if (isCurrent)
                        roomItem->setForeground(0, QColor("#7fb3d3"));

                    for (const QString &uid : userIds) {
                        auto *userItem = new QTreeWidgetItem(roomItem);
                        const int peerId = uid.toInt();
                        const bool isMe = (peerId == voiceChat->id());
                        const QString label = isMe ? QString("You")
                                                   : voipNicknames.value(peerId, uid);
                        userItem->setText(0, u8"\U0001F464 " + label);
                        userItem->setForeground(0, isMe ? QColor("#7fb3d3") : QColor("#aaaaaa"));
                        userItem->setData(0, Qt::UserRole, peerId);
                    }

                    roomItem->setExpanded(isCurrent || expanded.contains(roomName));
                }
            });
}

MainWindow::~MainWindow() = default;

void MainWindow::onVoipStatusChanged(const QString &status)
{
    if (voipStatusLabel)
        voipStatusLabel->setText(status);
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
    Q_UNUSED(ip)
    Q_UNUSED(port)
}

void MainWindow::onVoipPeerDisconnected(const QString &ip, quint16 port)
{
    Q_UNUSED(ip)
    Q_UNUSED(port)
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

    gitMenu->addAction("Init Repository", this, [this]() {
        if (!gitPanel_)
            return;
        const QString path = fileBrowser->rootPath().isEmpty()
                                 ? QFileDialog::getExistingDirectory(this, "Select Project Folder")
                                 : fileBrowser->rootPath();
        if (!path.isEmpty()) {
            gitPanel_->initRepo(path);
            bottomTabs->setCurrentWidget(gitPane);
            bottomDock->setVisible(true);
        }
    });

    gitMenu->addAction("Clone Repository...", this, &MainWindow::cloneRepo);

    gitMenu->addSeparator();

    auto *actRefresh = gitMenu->addAction("Refresh Status", this, [this]() {
        if (gitPanel_) {
            gitPanel_->refresh();
            bottomTabs->setCurrentWidget(gitPane);
            bottomDock->setVisible(true);
        }
    });
    actRefresh->setShortcut(QKeySequence("Ctrl+Shift+G"));

    gitMenu->addAction("Stage All", this, [this]() {
        if (gitPanel_) {
            gitPanel_->stageAll();
            bottomTabs->setCurrentWidget(gitPane);
            bottomDock->setVisible(true);
        }
    });

    gitMenu->addAction("Unstage All", this, [this]() {
        if (gitPanel_) {
            gitPanel_->unstageAll();
            bottomTabs->setCurrentWidget(gitPane);
            bottomDock->setVisible(true);
        }
    });

    gitMenu->addSeparator();

    auto *actCommit = gitMenu->addAction("Commit...", this, [this]() {
        if (gitPanel_) {
            bottomTabs->setCurrentWidget(gitPane);
            bottomDock->setVisible(true);
            gitPanel_->focusCommitMessage();
        }
    });
    actCommit->setShortcut(QKeySequence("Ctrl+K"));

    gitMenu->addSeparator();

    gitMenu->addAction("Pull", this, [this]() {
        if (gitPanel_) {
            gitPanel_->pull();
            bottomTabs->setCurrentWidget(outputPane);
            bottomDock->setVisible(true);
        }
    });

    gitMenu->addAction("Push", this, [this]() {
        if (gitPanel_) {
            gitPanel_->push();
            bottomTabs->setCurrentWidget(outputPane);
            bottomDock->setVisible(true);
        }
    });

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

    actDebugMain = tb->addAction("Debug", this, &MainWindow::startDebugging);
    actDebugMain->setToolTip("Start/Stop Debugging (F9)");
    actDebugMain->setShortcut(QKeySequence("F9"));

    tb->addSeparator();

    auto *actCollab = tb->addAction("Collab");
    actCollab->setCheckable(true);
    connect(actCollab, &QAction::triggered, this, [this, actCollab](bool checked) {
        if (checked) {
            if (!showStartCollabDialog())
                actCollab->setChecked(false);
        } else {
            stopAllCollab();
        }
    });

    auto *actCall = tb->addAction("Call");
    actCall->setToolTip("Toggle Voice");
    connect(actCall, &QAction::triggered, this, &MainWindow::toggleVoipDock);
}

void MainWindow::startCollab(const QString &room,
                             CollabSession::Mode mode,
                             const QStringList &selectedFiles)
{
    stopAllCollab();

    const int siteId = static_cast<int>(QRandomGenerator::global()->bounded(100000u, 999999u));

    session = new CollabSession(siteId, CollabSession::Role::Host, this);
    session->setCollabMode(mode);
    if (auth && auth->isLoggedIn())
        session->setUsername(auth->currentUser().username);

    const QString projectRoot = fileBrowser->rootPath();

    QStringList allRelPaths;
    QMap<QString, QString> fileTexts;

    QDirIterator it(projectRoot,
                    {"*.py", "*.cpp", "*.h", "*.pro", "*.txt", "*.md", "*.json"},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absPath = it.next();
        const QString relPath = QDir(projectRoot).relativeFilePath(absPath);
        allRelPaths.append(relPath);
        QFile f(absPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            fileTexts[relPath] = QString::fromUtf8(f.readAll());
    }

    for (int i = 0; i < editorTabs->count(); ++i) {
        auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (!ed || ed->getFilePath().isEmpty())
            continue;
        const QString absPath = ed->getFilePath();
        if (!absPath.startsWith(projectRoot))
            continue;
        const QString relPath = QDir(projectRoot).relativeFilePath(absPath);
        if (!allRelPaths.contains(relPath))
            allRelPaths.append(relPath);
        fileTexts[relPath] = ed->text();
    }

    if (!selectedFiles.isEmpty()) {
        QStringList filtered;
        for (const QString &p : std::as_const(allRelPaths))
            if (selectedFiles.contains(p))
                filtered.append(p);
        allRelPaths = filtered;
        for (auto jt = fileTexts.begin(); jt != fileTexts.end();)
            jt = selectedFiles.contains(jt.key()) ? ++jt : fileTexts.erase(jt);
    }

    for (auto it = fileTexts.cbegin(); it != fileTexts.cend(); ++it)
        session->initFileCache(it.key(), it.value());

    session->setProject(projectRoot, allRelPaths);

    for (int i = 0; i < editorTabs->count(); ++i) {
        auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (!ed || ed->getFilePath().isEmpty())
            continue;
        const QString absPath = ed->getFilePath();
        if (!absPath.startsWith(projectRoot))
            continue;
        const QString relPath = QDir(projectRoot).relativeFilePath(absPath);
        wireEditorToSession(ed, relPath);
        markTabAsCollab(ed, true);
    }

    connect(session, &CollabSession::usersUpdated, this, &MainWindow::onCollabUsersUpdated);
    connect(session, &CollabSession::projectInitReceived, this, &MainWindow::onSessionProjectInit);
    connect(session, &CollabSession::runOutputReceived, this, &MainWindow::onSessionRunOutput);
    connect(session, &CollabSession::remoteFileCreated, this, &MainWindow::onSessionFileCreated);
    connect(session, &CollabSession::remoteFileDeleted, this, &MainWindow::onSessionFileDeleted);
    connect(session, &CollabSession::remoteFileRenamed, this, &MainWindow::onSessionFileRenamed);
    connect(session,
            &CollabSession::remoteFileFocusChanged,
            this,
            &MainWindow::onRemoteFileFocusChanged);
    connect(session, &CollabSession::sessionReportReady, this, &MainWindow::onSessionReportReady);
    connect(session,
            &CollabSession::sessionAiInsightsReady,
            this,
            &MainWindow::onSessionAiInsightsReady);
    connect(session, &CollabSession::errorOccurred, this, [this](const QString &err) {
        outputPane->appendPlainText("[Collab] Error: " + err);
    });
    connect(session, &CollabSession::reconnecting, this, [this](int attempt, int maxAttempts) {
        const int delayS = 1 << (attempt - 1);
        const QString msg = QString("Reconnecting... attempt %1/%2 (in %3s)")
                                .arg(attempt)
                                .arg(maxAttempts)
                                .arg(delayS);
        outputPane->appendPlainText("[Collab] " + msg);
        if (collabStatusLabel)
            collabStatusLabel->setText(msg);
    });
    connect(session, &CollabSession::kicked, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            stopAllCollab();
            QMessageBox::information(this, "Collab", "You were kicked from the session.");
        });
    });

    connect(
        session,
        &CollabSession::connected,
        this,
        [this, fileTexts]() {
            session->sendAllSnapshots(fileTexts);
            outputPane->appendPlainText("[Collab] Project session started — you are host");
            if (collabStatusLabel)
                collabStatusLabel->setText("Hosting");
            if (collabNoSessionPane)
                collabNoSessionPane->hide();
            if (collabInSessionPane)
                collabInSessionPane->show();
            const QString path = editor ? editor->getFilePath() : QString();
            if (!path.isEmpty()) {
                const QString relPath = toSessionKey(path);
                currentCollabFile = relPath;
                session->sendFileFocus(relPath);
                peerFiles[session->siteId()] = relPath;
                refreshCollabUsersList();
            }
        },
        Qt::SingleShotConnection);

    session->connectToServer(QString("ws://localhost:8765/%1").arg(room));
    outputPane->appendPlainText("[Collab] Starting room: " + room);
}

void MainWindow::joinCollab()
{
    bool ok = false;
    const QString room = QInputDialog::getText(this,
                                               "Join Collaboration",
                                               "Enter room name:",
                                               QLineEdit::Normal,
                                               "",
                                               &ok)
                             .trimmed();
    if (!ok || room.isEmpty())
        return;

    stopAllCollab();

    const int siteId = static_cast<int>(QRandomGenerator::global()->bounded(100000u, 999999u));

    session = new CollabSession(siteId, CollabSession::Role::Guest, this);
    if (auth && auth->isLoggedIn())
        session->setUsername(auth->currentUser().username);

    connect(session, &CollabSession::projectInitReceived, this, &MainWindow::onSessionProjectInit);
    connect(session, &CollabSession::runOutputReceived, this, &MainWindow::onSessionRunOutput);
    connect(session, &CollabSession::usersUpdated, this, &MainWindow::onCollabUsersUpdated);
    connect(session, &CollabSession::remoteFileCreated, this, &MainWindow::onSessionFileCreated);
    connect(session, &CollabSession::remoteFileDeleted, this, &MainWindow::onSessionFileDeleted);
    connect(session, &CollabSession::remoteFileRenamed, this, &MainWindow::onSessionFileRenamed);
    connect(session,
            &CollabSession::remoteFileFocusChanged,
            this,
            &MainWindow::onRemoteFileFocusChanged);
    connect(session, &CollabSession::sessionReportReady, this, &MainWindow::onSessionReportReady);
    connect(session,
            &CollabSession::sessionAiInsightsReady,
            this,
            &MainWindow::onSessionAiInsightsReady);
    connect(session, &CollabSession::errorOccurred, this, [this](const QString &err) {
        outputPane->appendPlainText("[Collab] Error: " + err);
    });
    connect(session, &CollabSession::reconnecting, this, [this](int attempt, int maxAttempts) {
        const int delayS = 1 << (attempt - 1);
        const QString msg = QString("Reconnecting... attempt %1/%2 (in %3s)")
                                .arg(attempt)
                                .arg(maxAttempts)
                                .arg(delayS);
        outputPane->appendPlainText("[Collab] " + msg);
        if (collabStatusLabel)
            collabStatusLabel->setText(msg);
    });
    connect(session, &CollabSession::kicked, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            stopAllCollab();
            QMessageBox::information(this, "Collab", "You were kicked from the session.");
        });
    });
    connect(session, &CollabSession::disconnected, this, [this]() {
        if (pendingEndCollab)
            return;
        QTimer::singleShot(0, this, [this]() {
            stopAllCollab();
            outputPane->appendPlainText("[Collab] Host left — session ended.");
        });
    });
    connect(
        session,
        &CollabSession::connected,
        this,
        [this]() {
            outputPane->appendPlainText("[Collab] Connected — waiting for project info…");
            if (collabStatusLabel)
                collabStatusLabel->setText("Guest — connected");
            if (collabNoSessionPane)
                collabNoSessionPane->hide();
            if (collabInSessionPane)
                collabInSessionPane->show();
        },
        Qt::SingleShotConnection);

    session->connectToServer(QString("ws://localhost:8765/%1").arg(room));
    outputPane->appendPlainText("[Collab] Joining room: " + room);
}

void MainWindow::wireEditorToManager(CodeEditor *ed, RGAManager *mgr)
{
    connect(ed, &CodeEditor::localInsert, mgr, &RGAManager::localInsert, Qt::UniqueConnection);
    connect(ed, &CodeEditor::localDelete, mgr, &RGAManager::localRemove, Qt::UniqueConnection);
    connect(ed, &CodeEditor::undoRequested, mgr, &RGAManager::undo, Qt::UniqueConnection);
    connect(ed, &CodeEditor::redoRequested, mgr, &RGAManager::redo, Qt::UniqueConnection);
    connect(ed, &CodeEditor::beginUndoGroup, mgr, &RGAManager::beginGroup, Qt::UniqueConnection);
    connect(ed, &CodeEditor::endUndoGroup, mgr, &RGAManager::endGroup, Qt::UniqueConnection);
    ed->collabActive = true;

    connect(mgr, &RGAManager::remoteTextChanged, ed, [ed](const QString &newText) {
        ed->applyRemoteText(newText);
    });

    connect(ed, &CodeEditor::cursorPositionUpdated, mgr, [mgr, ed](int, int) {
        mgr->sendCursorPosition(ed->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    });

    connect(mgr,
            &RGAManager::remoteCursorMoved,
            ed,
            &CodeEditor::updateRemoteCursor,
            Qt::UniqueConnection);

    connect(mgr,
            &RGAManager::remoteCursorLeft,
            ed,
            &CodeEditor::removeRemoteCursor,
            Qt::UniqueConnection);
}

void MainWindow::wireEditorToSession(CodeEditor *ed, const QString &relPath)
{
    RGAManager *mgr = session->getOrCreateRGA(relPath);
    wireEditorToManager(ed, mgr);

    const auto cached = session->fileCursors(relPath);
    for (auto it = cached.cbegin(); it != cached.cend(); ++it)
        ed->updateRemoteCursor(it.key(), it.value());

    for (auto it = peerNames.cbegin(); it != peerNames.cend(); ++it)
        ed->setRemotePeerName(it.key(), it.value());

    if (session->role() == CollabSession::Role::Guest
        && session->collabMode() == CollabSession::Mode::ReadOnly)
        ed->setReadOnly(true);
}

QString MainWindow::toSessionKey(const QString &editorPath) const
{
    if (!session || session->role() == CollabSession::Role::Guest)
        return editorPath;
    const QString root = session->projectRoot();
    return root.isEmpty() ? editorPath : QDir(root).relativeFilePath(editorPath);
}

QMap<QString, QString> MainWindow::collectCurrentFileTexts() const
{
    QMap<QString, QString> texts;
    if (!session)
        return texts;

    const QString root = session->projectRoot();

    for (int i = 0; i < editorTabs->count(); ++i) {
        const auto *ed = qobject_cast<const CodeEditor *>(editorTabs->widget(i));
        if (!ed)
            continue;
        const QString abs = ed->getFilePath();
        if (abs.isEmpty())
            continue;
        const QString rel = root.isEmpty() ? abs : QDir(root).relativeFilePath(abs);
        texts[rel] = ed->text();
    }

    for (const QString &f : session->fileList())
        if (!texts.contains(f) && session->hasTextCache(f))
            texts[f] = session->cachedText(f);

    return texts;
}

void MainWindow::stopAllCollab()
{
    pendingEndCollab = false;
    if (!session)
        return;

    const bool isGuest = (session->role() == CollabSession::Role::Guest);

    if (isGuest) {
        for (int i = editorTabs->count() - 1; i >= 0; --i) {
            auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
            if (ed && ed->collabActive) {
                editorTabs->removeTab(i);
                ed->deleteLater();
            }
        }
    } else {
        for (int i = 0; i < editorTabs->count(); ++i) {
            auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
            if (!ed)
                continue;
            ed->clearRemoteCursors();
            ed->collabActive = false;
            markTabAsCollab(ed, false);
        }
    }

    session->disconnectFromServer();
    session->deleteLater();
    session = nullptr;

    peerFiles.clear();
    peerNames.clear();
    currentCollabFile.clear();

    if (collabUsersList)
        collabUsersList->clear();
    if (collabStatusLabel)
        collabStatusLabel->setText("Not in collab");
    if (collabNoSessionPane)
        collabNoSessionPane->show();
    if (collabInSessionPane)
        collabInSessionPane->hide();

    fileBrowser->clearRemoteMode();

    outputPane->appendPlainText("[Collab] Session stopped.");
}

void MainWindow::markTabAsCollab(CodeEditor *ed, bool on)
{
    const int idx = editorTabs->indexOf(ed);
    if (idx < 0)
        return;

    QString name = QFileInfo(ed->getFilePath()).fileName();
    if (name.isEmpty())
        name = "Untitled";

    editorTabs->setTabText(idx, on ? (QString::fromUtf8("◎ ") + name) : name);
}

void MainWindow::refreshCollabUsersList()
{
    if (!collabUsersList || !session)
        return;
    collabUsersList->clear();
    for (auto it = peerNames.cbegin(); it != peerNames.cend(); ++it) {
        const int id = it.key();
        const QString name = it.value().isEmpty() ? QString("user_%1").arg(id) : it.value();
        QString label = name;
        if (id == session->siteId())
            label += " (you)";
        const QString f = peerFiles.value(id);
        if (!f.isEmpty())
            label += QString("  [%1]").arg(QFileInfo(f).fileName());
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, id);
        collabUsersList->addItem(item);
    }
}

void MainWindow::onCollabUsersUpdated(QMap<int, QString> users)
{
    peerNames = users;
    refreshCollabUsersList();

    for (int i = 0; i < editorTabs->count(); ++i) {
        if (auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i)))
            for (auto it = users.cbegin(); it != users.cend(); ++it)
                ed->setRemotePeerName(it.key(), it.value());
    }
    if (collabStatusLabel && !users.isEmpty()) {
        const QString role = (session && session->role() == CollabSession::Role::Host) ? "Hosting"
                                                                                       : "Guest";
        collabStatusLabel->setText(QString("%1 — %2 user(s)").arg(role).arg(users.size()));
    }
}

void MainWindow::onRemoteFileFocusChanged(int siteId, const QString &file)
{
    peerFiles[siteId] = file;
    refreshCollabUsersList();
}

void MainWindow::onTabCloseRequested(int tabIndex)
{
    if (editorTabs->count() <= 1)
        return;

    CodeEditor *tabEditor = qobject_cast<CodeEditor *>(editorTabs->widget(tabIndex));
    if (!tabEditor)
        return;

    if (session) {
        const QString relPath = toSessionKey(tabEditor->getFilePath());
        if (session->hasActiveRGA(relPath)) {
            tabEditor->clearRemoteCursors();
            session->releaseRGA(relPath);
        }
        markTabAsCollab(tabEditor, false);
    }

    if (tabEditor->isModified() && !(session && session->role() == CollabSession::Role::Guest)) {
        const auto btn = QMessageBox::question(this,
                                               "Unsaved Changes",
                                               "Save changes before closing this tab?",
                                               QMessageBox::Save | QMessageBox::Discard
                                                   | QMessageBox::Cancel);
        if (btn == QMessageBox::Save) {
            editor = tabEditor;
            if (!saveFile())
                return;
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

    auto *btnCollab = makeBtn("Collab", "Collaboration");
    vbox->addWidget(btnCollab);
    connect(btnCollab, &QToolButton::clicked, this, [this] { onActivityButton(3); });

    btnVoip = makeBtn("VoIP", "Voice");
    btnVoip->setObjectName("activityBtnVoip");
    vbox->addWidget(btnVoip);

    btnProfile = new QToolButton;
    btnProfile->setText("Account");
    btnProfile->setToolTip("Sign in");
    btnProfile->setCheckable(false);
    btnProfile->setFixedSize(48, 48);
    btnProfile->setObjectName("activityBtnProfile");
    vbox->addWidget(btnProfile);

    connect(btnFiles, &QToolButton::clicked, this, [this] { onActivityButton(0); });
    connect(btnTasks, &QToolButton::clicked, this, [this] { onActivityButton(1); });
    connect(btnTeam, &QToolButton::clicked, this, [this] { onActivityButton(2); });
    connect(btnVoip, &QToolButton::clicked, this, &MainWindow::toggleVoipDock);
    connect(btnProfile, &QToolButton::clicked, this, [this] {
        if (!auth || !auth->isLoggedIn()) {
            AuthDialog dlg(auth, this);
            if (dlg.exec() == QDialog::Accepted)
                updateProfileButton();
            return;
        }
        QMenu menu(this);
        const QString uname = auth->currentUser().username;
        auto *title = menu.addAction(uname);
        title->setEnabled(false);
        menu.addSeparator();
        menu.addAction("Sign out", this, [this] { auth->logout(); });
        menu.exec(btnProfile->mapToGlobal(btnProfile->rect().topRight()));
    });
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
    auto *collabPane = new QWidget;
    auto *vl = new QVBoxLayout(collabPane);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(6);

    collabStatusLabel = new QLabel("Not in collab");
    collabStatusLabel->setObjectName("stubLabel");
    collabStatusLabel->setWordWrap(true);
    vl->addWidget(collabStatusLabel);

    collabNoSessionPane = new QWidget;
    auto *noVl = new QVBoxLayout(collabNoSessionPane);
    noVl->setContentsMargins(0, 4, 0, 0);
    noVl->setSpacing(4);

    btnStartCollab = new QPushButton("Start Collab");
    btnStartCollab->setObjectName("voipBtn");
    connect(btnStartCollab, &QPushButton::clicked, this, [this]() { showStartCollabDialog(); });
    noVl->addWidget(btnStartCollab);

    btnJoinCollab = new QPushButton("Join Collab");
    btnJoinCollab->setObjectName("voipBtn");
    connect(btnJoinCollab, &QPushButton::clicked, this, &MainWindow::joinCollab);
    noVl->addWidget(btnJoinCollab);

    noVl->addStretch(1);
    vl->addWidget(collabNoSessionPane, 1);

    collabInSessionPane = new QWidget;
    auto *inVl = new QVBoxLayout(collabInSessionPane);
    inVl->setContentsMargins(0, 4, 0, 0);
    inVl->setSpacing(4);

    auto *usersLbl = new QLabel("PARTICIPANTS");
    usersLbl->setObjectName("stubLabel");
    inVl->addWidget(usersLbl);

    collabUsersList = new QListWidget;
    collabUsersList->setObjectName("taskList");
    collabUsersList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(collabUsersList,
            &QListWidget::customContextMenuRequested,
            this,
            &MainWindow::onCollabUserContextMenu);
    inVl->addWidget(collabUsersList, 1);

    btnSessionReport = new QPushButton("Session Report");
    btnSessionReport->setObjectName("voipBtn");
    connect(btnSessionReport, &QPushButton::clicked, this, [this]() {
        if (!session)
            return;
        session->sendFinalStates(collectCurrentFileTexts());
        session->requestSessionReport();
    });
    inVl->addWidget(btnSessionReport);

    btnStopAllCollab = new QPushButton("End Collab");
    btnStopAllCollab->setObjectName("voipBtn");
    connect(btnStopAllCollab, &QPushButton::clicked, this, &MainWindow::onEndCollabRequested);
    inVl->addWidget(btnStopAllCollab);

    collabInSessionPane->hide();
    vl->addWidget(collabInSessionPane, 1);

    leftStack->addWidget(collabPane);

    connect(fileBrowser, &FileBrowser::fileDoubleClicked, this, &MainWindow::openFileFromBrowser);
}

void MainWindow::openFileFromBrowser(const QString &path)
{
    for (int i = 0; i < editorTabs->count(); i++) {
        CodeEditor *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (ed && ed->getFilePath() == path) {
            editorTabs->setCurrentIndex(i);
            return;
        }
    }

    CodeEditor *newEditor = new CodeEditor(editorTabs);

    const bool isGuest = session && session->role() == CollabSession::Role::Guest;

    if (isGuest) {
        newEditor->setFilePath(path);
        const QString cached = session->cachedText(path);
        if (!cached.isEmpty())
            newEditor->applyRemoteText(cached);
    } else if (session) {
        const QString relPath = toSessionKey(path);
        newEditor->setFilePath(path);
        const QString cached = session->cachedText(relPath);
        if (!cached.isEmpty())
            newEditor->applyRemoteText(cached);
        else
            newEditor->loadFile(path);
    } else {
        newEditor->loadFile(path);
    }

    connect(newEditor,
            &CodeEditor::cursorPositionUpdated,
            this,
            &MainWindow::onCursorPositionUpdated);
    connect(newEditor, &CodeEditor::modifyChanged, this, &MainWindow::onModificationChanged);

    const QString name = QFileInfo(path).fileName();
    const int tabIdx = editorTabs->addTab(newEditor, name);
    editorTabs->setCurrentIndex(tabIdx);

    if (session) {
        const QString relPath = toSessionKey(path);
        if (!isGuest && !session->hasTextCache(relPath))
            session->initFileCache(relPath, newEditor->text());

        wireEditorToSession(newEditor, relPath);

        if (!isGuest) {
            RGAManager *mgr = session->getOrCreateRGA(relPath);
            if (mgr->getText().isEmpty()) {
                mgr->buildFromText(newEditor->text());
                mgr->sendInitText(newEditor->text(), relPath);
            }
        }
        markTabAsCollab(newEditor, true);
    }

    currentFilePath = path;
    updateWindowTitle();
    if (!isGuest)
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

    gitPanel_ = new GitPanel(this);
    gitPane = gitPanel_;
    connect(gitPanel_, &GitPanel::logMessage, this, [this](const QString &msg) {
        outputPane->appendPlainText(msg);
    });
    connect(gitPanel_, &GitPanel::diffRequested, this, &MainWindow::openDiffTab);
    bottomTabs->addTab(gitPane, "Git");

    setupDebugPanel();

    bottomDock->setWidget(bottomTabs);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    resizeDocks({bottomDock}, {180}, Qt::Vertical);
}

void MainWindow::setupDebugPanel()
{
    debugPane = new QWidget;
    debugPane->setObjectName("debugPane");

    auto *root = new QVBoxLayout(debugPane);
    root->setContentsMargins(0, 2, 0, 0);
    root->setSpacing(2);

    auto *dbgBar = new QToolBar;
    dbgBar->setMovable(false);
    dbgBar->setStyleSheet(R"(
        QToolBar {
            background: #252526;
            border: none;
            spacing: 3px;
            padding: 3px 6px;
        }
        QToolButton {
            background: #3a3d41;
            color: #d4d4d4;
            border: 1px solid #555558;
            border-radius: 4px;
            padding: 4px 11px;
            font-size: 12px;
        }
        QToolButton:hover {
            background: #4a4d51;
            border-color: #569cd6;
            color: #ffffff;
        }
        QToolButton:pressed { background: #2a2d31; }
        QToolButton:disabled { color: #555558; background: #2d2d2d; border-color: #3a3a3a; }
        QToolBar::separator { background: #555558; width: 1px; margin: 4px 3px; }
    )");

    actDebugContinue = dbgBar->addAction(u8"▶  Continue", this, [this] {
        if (debugAdapter)
            debugAdapter->continueExec();
    });
    actDebugContinue->setShortcut(QKeySequence("F5"));
    actDebugContinue->setToolTip("Continue (F5)");

    actDebugStepOver = dbgBar->addAction(u8"↪  Step Over", this, [this] {
        if (debugAdapter)
            debugAdapter->stepOver();
    });
    actDebugStepOver->setShortcut(QKeySequence("F10"));
    actDebugStepOver->setToolTip("Step Over (F10)");

    actDebugStepIn = dbgBar->addAction(u8"↓  Step Into", this, [this] {
        if (debugAdapter)
            debugAdapter->stepIn();
    });
    actDebugStepIn->setShortcut(QKeySequence("F11"));
    actDebugStepIn->setToolTip("Step Into (F11)");

    actDebugStepOut = dbgBar->addAction(u8"↑  Step Out", this, [this] {
        if (debugAdapter)
            debugAdapter->stepOut();
    });
    actDebugStepOut->setShortcut(QKeySequence("Shift+F11"));
    actDebugStepOut->setToolTip("Step Out (Shift+F11)");

    dbgBar->addSeparator();

    actDebugStop = dbgBar->addAction(u8"■  Stop", this, &MainWindow::stopDebugging);
    actDebugStop->setShortcut(QKeySequence("Shift+F9"));
    actDebugStop->setToolTip("Stop Debugging (Shift+F9)");

    for (auto *a :
         {actDebugContinue, actDebugStepOver, actDebugStepIn, actDebugStepOut, actDebugStop})
        a->setEnabled(false);

    if (auto *btn = qobject_cast<QToolButton *>(dbgBar->widgetForAction(actDebugContinue)))
        btn->setStyleSheet("QToolButton { color: #4ec9b0; } "
                           "QToolButton:hover { color: #6fdfc8; } "
                           "QToolButton:disabled { color: #2a5a52; }");
    if (auto *btn = qobject_cast<QToolButton *>(dbgBar->widgetForAction(actDebugStop)))
        btn->setStyleSheet("QToolButton { color: #f48771; } "
                           "QToolButton:hover { color: #ff9f8f; } "
                           "QToolButton:disabled { color: #5a2a22; }");

    root->addWidget(dbgBar);

    auto *splitter = new QSplitter(Qt::Horizontal);

    debugCallStack = new QTreeWidget;
    debugCallStack->setObjectName("debugCallStack");
    debugCallStack->setHeaderLabel("Call Stack");
    debugCallStack->setRootIsDecorated(false);
    connect(debugCallStack, &QTreeWidget::itemClicked, this, &MainWindow::onDebugCallStackClicked);
    splitter->addWidget(debugCallStack);

    debugVariables = new QTreeWidget;
    debugVariables->setObjectName("debugVariables");
    debugVariables->setColumnCount(2);
    debugVariables->setHeaderLabels({"Name", "Value"});
    debugVariables->setRootIsDecorated(true);
    connect(debugVariables, &QTreeWidget::itemExpanded, this, &MainWindow::onDebugVariableExpanded);
    splitter->addWidget(debugVariables);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    root->addWidget(splitter, 1);

    bottomTabs->addTab(debugPane, "Debug");
}

void MainWindow::startDebugging()
{
    if (debugAdapter && debugAdapter->isRunning()) {
        stopDebugging();
        return;
    }

    CodeEditor *ed = qobject_cast<CodeEditor *>(editorTabs->currentWidget());
    if (!ed)
        return;

    const QString path = ed->getFilePath();
    if (path.isEmpty()) {
        outputPane->appendPlainText("[Debug] Save the file before debugging.");
        bottomTabs->setCurrentWidget(outputPane);
        return;
    }

    if (!path.endsWith(".py", Qt::CaseInsensitive)) {
        outputPane->appendPlainText("[Debug] Debugger supports Python (.py) files only.");
        bottomTabs->setCurrentWidget(outputPane);
        return;
    }

    delete debugAdapter;
    debugAdapter = new DebugAdapter(this);

    connect(debugAdapter, &DebugAdapter::logMessage, this, [this](const QString &txt) {
        outputPane->appendPlainText(txt.trimmed());
    });
    connect(debugAdapter, &DebugAdapter::stopped, this, &MainWindow::onDebugStopped);
    connect(debugAdapter, &DebugAdapter::continued, this, &MainWindow::onDebugContinued);
    connect(debugAdapter, &DebugAdapter::terminated, this, &MainWindow::onDebugTerminated);
    connect(debugAdapter, &DebugAdapter::callStackReady, this, &MainWindow::onDebugCallStackReady);
    connect(debugAdapter, &DebugAdapter::variablesReady, this, &MainWindow::onDebugVariablesReady);
    connect(debugAdapter,
            &DebugAdapter::subVariablesReady,
            this,
            &MainWindow::onDebugSubVariablesReady);
    connect(ed, &CodeEditor::breakpointsChanged, debugAdapter, &DebugAdapter::updateBreakpoints);

    for (auto *a :
         {actDebugContinue, actDebugStepOver, actDebugStepIn, actDebugStepOut, actDebugStop})
        a->setEnabled(false);

    actDebugStop->setEnabled(true);
    actDebugMain->setText("Stop Debug");

    outputPane->appendPlainText(QString("[Debug] Starting: %1").arg(path));
    outputPane->appendPlainText("[Debug] Install debugpy if missing:  pip install debugpy");
    bottomTabs->setCurrentWidget(outputPane);

    debugAdapter->startDebugging(path, ed->breakpoints());
}

void MainWindow::stopDebugging()
{
    if (debugAdapter)
        debugAdapter->stop();
}

void MainWindow::onDebugStopped(const QString &filePath, int line, const QString &)
{
    clearDebugHighlights();

    auto normPath = [](const QString &p) {
        return QDir::cleanPath(QDir::fromNativeSeparators(p)).toLower();
    };
    const QString stoppedNorm = normPath(filePath);

    for (int i = 0; i < editorTabs->count(); ++i) {
        auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (!ed)
            continue;
        if (normPath(ed->getFilePath()) == stoppedNorm) {
            editorTabs->setCurrentIndex(i);
            ed->setDebugLine(line);
            break;
        }
    }

    for (auto *a : {actDebugContinue, actDebugStepOver, actDebugStepIn, actDebugStepOut})
        a->setEnabled(true);

    bottomTabs->setCurrentWidget(debugPane);
}

void MainWindow::onDebugContinued()
{
    clearDebugHighlights();
    for (auto *a : {actDebugContinue, actDebugStepOver, actDebugStepIn, actDebugStepOut})
        a->setEnabled(false);
}

void MainWindow::onDebugTerminated()
{
    clearDebugHighlights();
    for (auto *a :
         {actDebugContinue, actDebugStepOver, actDebugStepIn, actDebugStepOut, actDebugStop})
        a->setEnabled(false);

    actDebugMain->setText("Debug");
    debugCallStack->clear();
    debugVariables->clear();
    outputPane->appendPlainText("[Debug] Session ended.");
    bottomTabs->setCurrentWidget(outputPane);
}

void MainWindow::onDebugCallStackReady(const QList<DebugAdapter::FrameInfo> &frames)
{
    debugCallStack->clear();
    for (const auto &f : frames) {
        auto *item = new QTreeWidgetItem(debugCallStack, {f.label});
        item->setData(0, Qt::UserRole, f.frameId);
    }
}

void MainWindow::onDebugCallStackClicked(QTreeWidgetItem *item, int)
{
    if (!debugAdapter || !debugAdapter->isRunning())
        return;
    const QVariant d = item->data(0, Qt::UserRole);
    if (!d.isValid())
        return;
    const int frameId = d.toInt();
    if (frameId >= 0)
        debugAdapter->requestFrameVariables(frameId);
}

static QTreeWidgetItem *dbgFindByRef(QTreeWidgetItem *parent, int ref)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        auto *it = parent->child(i);
        if (it->data(0, Qt::UserRole).toInt() == ref)
            return it;
        auto *found = dbgFindByRef(it, ref);
        if (found)
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem *dbgMakeVarItem(const DebugAdapter::Var &v, QTreeWidgetItem *parent)
{
    auto *item = new QTreeWidgetItem(parent, {v.name, v.value});
    item->setData(0, Qt::UserRole, v.ref);
    if (v.ref > 0)
        new QTreeWidgetItem(item);
    return item;
}

void MainWindow::onDebugVariablesReady(const QList<DebugAdapter::Var> &vars)
{
    debugVariables->clear();

    static const QStringList kCollapsed = {"special variables",
                                           "function variables",
                                           "protected variables",
                                           "class variables"};

    for (const auto &v : vars) {
        auto *item = dbgMakeVarItem(v, debugVariables->invisibleRootItem());
        debugVariables->addTopLevelItem(item);
        if (kCollapsed.contains(v.name.toLower()))
            item->setExpanded(false);
    }
    debugVariables->resizeColumnToContents(0);
}

void MainWindow::onDebugSubVariablesReady(int parentRef, const QList<DebugAdapter::Var> &vars)
{
    auto *parent = dbgFindByRef(debugVariables->invisibleRootItem(), parentRef);
    if (!parent)
        return;

    qDeleteAll(parent->takeChildren());

    for (const auto &v : vars)
        dbgMakeVarItem(v, parent);

    debugVariables->resizeColumnToContents(0);
}

void MainWindow::onDebugVariableExpanded(QTreeWidgetItem *item)
{
    const int ref = item->data(0, Qt::UserRole).toInt();
    if (ref <= 0)
        return;

    if (item->childCount() == 1 && item->child(0)->text(0).isEmpty()) {
        if (debugAdapter)
            debugAdapter->requestSubVariables(ref);
    }
}

void MainWindow::clearDebugHighlights()
{
    for (int i = 0; i < editorTabs->count(); ++i) {
        auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (ed)
            ed->clearDebugLine();
    }
}

void MainWindow::openDiffTab(const QString &relPath, bool staged)
{
    if (!gitPanel_)
        return;

    const QString workdir = gitPanel_->manager()->workdir();
    const QString absPath = QDir::cleanPath(workdir + relPath);
    const QString tabName = QFileInfo(relPath).fileName() + " (diff)";

    auto applyDiff = [&](CodeEditor *ed) {
        const QString raw = staged ? gitPanel_->manager()->diffStaged(relPath)
                                   : gitPanel_->manager()->diffUnstaged(relPath);
        ed->applyDiffText(raw);
    };

    for (int i = 0; i < editorTabs->count(); ++i) {
        auto *ed = qobject_cast<CodeEditor *>(editorTabs->widget(i));
        if (ed && ed->property("diffPath").toString() == absPath
            && ed->property("diffStaged").toBool() == staged) {
            applyDiff(ed);
            editorTabs->setCurrentIndex(i);
            return;
        }
    }

    CodeEditor *ed = new CodeEditor(editorTabs);
    ed->setProperty("diffPath", absPath);
    ed->setProperty("diffStaged", staged);
    applyDiff(ed);

    const int idx = editorTabs->addTab(ed, tabName);
    editorTabs->setCurrentIndex(idx);
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

    voipTree = new QTreeWidget;
    voipTree->setObjectName("voipTree");
    voipTree->setHeaderHidden(true);
    voipTree->setIndentation(16);
    voipTree->setRootIsDecorated(true);
    voipTree->setExpandsOnDoubleClick(false);
    root->addWidget(voipTree, 1);

    btnCreateRoom = new QPushButton("Create Room");
    btnCreateRoom->setObjectName("voipBtn");
    root->addWidget(btnCreateRoom);

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

    if (session && session->isConnected()) {
        if (!currentCollabFile.isEmpty())
            session->sendCursorLeave(currentCollabFile);

        const QString relPath = toSessionKey(path);
        if (!path.isEmpty()) {
            currentCollabFile = relPath;
            session->sendFileFocus(relPath);
            peerFiles[session->siteId()] = relPath;
            refreshCollabUsersList();
        } else {
            currentCollabFile.clear();
        }
    }
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
QToolButton#activityBtnProfile {
    color: #858585;
    background: transparent;
    border: none;
    border-left: 2px solid transparent;
    font-size: 9px;
}
QToolButton#activityBtnProfile:hover { color: #cccccc; }
QToolButton#activityBtnProfile[loggedIn="true"] { color: #4ec9b0; }

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
QTreeWidget#voipTree {
    background: #1e1e1e;
    color: #d4d4d4;
    border: 1px solid #333;
}
QTreeWidget#voipTree::item {
    padding: 3px 0;
}
QTreeWidget#voipTree::item:selected {
    background: #094771;
    color: #ffffff;
}
QTreeWidget#voipTree::item:hover {
    background: #2a2d2e;
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

/* ── Git Panel ──────────────────────────────────────────────── */
QWidget#gitTopBar {
    background: #252526;
}
QLabel#gitSectionHeader {
    background: #2d2d30;
    color: #9d9d9d;
    font-size: 10px;
    padding-left: 4px;
    letter-spacing: 1px;
    border-bottom: 1px solid #3c3c3c;
}
QSplitter#gitSplitter::handle {
    background: #3c3c3c;
    width: 1px;
}
QTreeWidget#gitLogTree {
    background: #252526;
    color: #cccccc;
    border: none;
    outline: 0;
    alternate-background-color: #2a2a2a;
}
QTreeWidget#gitLogTree::item {
    height: 22px;
    padding: 1px 2px;
    border: none;
}
QTreeWidget#gitLogTree::item:hover    { background: #2a2d2e; }
QTreeWidget#gitLogTree::item:selected { background: #094771; color: #ffffff; }
QHeaderView#gitLogTree::section,
QTreeWidget#gitLogTree QHeaderView::section {
    background: #2d2d30;
    color: #9d9d9d;
    border: none;
    border-right: 1px solid #3c3c3c;
    border-bottom: 1px solid #3c3c3c;
    padding: 3px 6px;
    font-size: 11px;
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
    if (session && session->role() == CollabSession::Role::Guest) {
        bottomTabs->setCurrentWidget(outputPane);
        bottomDock->setVisible(true);
        outputPane->appendPlainText("[Collab] Code execution is controlled by the host.");
        return;
    }

    CodeEditor *currentEditor = qobject_cast<CodeEditor *>(editorTabs->currentWidget());
    if (!currentEditor)
        return;

    QString path = currentEditor->getFilePath();
    if (path.isEmpty()) {
        outputPane->appendPlainText("[TeamHub] Save file before running.");
        return;
    }

    if (QFileInfo(path).isRelative() && !fileBrowser->rootPath().isEmpty())
        path = QDir(fileBrowser->rootPath()).absoluteFilePath(path);

    const QString pythonvenv = fileBrowser->findFile("python.exe");
    const QString pythonpath = !pythonvenv.isEmpty() ? pythonvenv : "python";
    currentEditor->saveFile(path);
    outputPane->clear();
    bottomTabs->setCurrentWidget(outputPane);
    bottomDock->setVisible(true);

    QProcess *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        const QString chunk = QString::fromLocal8Bit(proc->readAllStandardOutput());
        outputPane->appendPlainText(chunk);
        if (session)
            session->broadcastRunOutput(chunk);
    });

    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, proc](int code, QProcess::ExitStatus) {
                const QString msg = QString("\n[TeamHub] Exit code: %1").arg(code);
                outputPane->appendPlainText(msg);
                if (session)
                    session->broadcastRunOutput(msg);
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

    if (gitPanel_)
        gitPanel_->setRepoPath(path);
}

void MainWindow::cloneRepo()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Clone Repository");
    dlg.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dlg);

    auto *urlEdit = new QLineEdit(&dlg);
    urlEdit->setPlaceholderText("https://github.com/user/repo.git");
    layout->addWidget(new QLabel("Repository URL:", &dlg));
    layout->addWidget(urlEdit);

    auto *pathRow = new QHBoxLayout;
    auto *pathEdit = new QLineEdit(&dlg);
    pathEdit->setPlaceholderText("Select destination folder...");
    auto *browseBtn = new QPushButton("Browse...", &dlg);
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseBtn);
    layout->addWidget(new QLabel("Destination folder:", &dlg));
    layout->addLayout(pathRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(&dlg, "Select Destination Folder");
        if (!dir.isEmpty())
            pathEdit->setText(dir);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString url = urlEdit->text().trimmed();
    const QString dest = pathEdit->text().trimmed();
    if (url.isEmpty() || dest.isEmpty())
        return;

    outputPane->appendPlainText("[Git] Cloning " + url + " into " + dest + "...");
    bottomDock->setVisible(true);

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(dest);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        outputPane->appendPlainText(QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed());
    });

    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, proc, url, dest](int code, QProcess::ExitStatus) {
                if (code == 0) {
                    const QString repoName = url.section('/', -1).remove(".git");
                    const QString clonedPath = dest + "/" + repoName;
                    outputPane->appendPlainText("[Git] Clone successful.");
                    fileBrowser->setRootPath(clonedPath);
                    clearTabs();
                    editor = new CodeEditor(editorTabs);
                    editorTabs->addTab(editor, "Untitled");
                    currentFilePath.clear();
                    setWindowTitle(repoName + " — TeamHub");
                    terminal->setWorkingDirectory(clonedPath);
                    if (gitPanel_)
                        gitPanel_->setRepoPath(clonedPath);
                } else {
                    outputPane->appendPlainText("[Git] Clone failed (exit code "
                                                + QString::number(code) + ").");
                }
                proc->deleteLater();
            });

    proc->start("git", {"clone", url, dest + "/" + url.section('/', -1).remove(".git")});
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

    if (willShow) {
        if (!voiceChat->isConnected())
            voiceChat->connectToServer("localhost", 9000);
        else
            voiceChat->requestRooms();
    }
}

void MainWindow::onVoipPeersUpdated(const QStringList &ids)
{
    for (int i = 0; i < voipTree->topLevelItemCount(); i++) {
        auto *roomItem = voipTree->topLevelItem(i);
        if (roomItem->data(0, Qt::UserRole).toString() != currentVoiceRoom)
            continue;

        qDeleteAll(roomItem->takeChildren());

        auto *meItem = new QTreeWidgetItem(roomItem);
        meItem->setText(0, u8"\U0001F464 You");
        meItem->setForeground(0, QColor("#7fb3d3"));
        meItem->setData(0, Qt::UserRole, voiceChat->id());

        for (const QString &uid : ids) {
            auto *userItem = new QTreeWidgetItem(roomItem);
            const int peerId = uid.toInt();
            userItem->setText(0, u8"\U0001F464 " + voipNicknames.value(peerId, uid));
            userItem->setForeground(0, QColor("#aaaaaa"));
            userItem->setData(0, Qt::UserRole, peerId);
        }

        roomItem->setExpanded(true);
        return;
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

    connect(voipTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item->parent())
            joinRoom(item->data(0, Qt::UserRole).toString());
    });

    connect(btnMuteMic, &QPushButton::toggled, this, [this](bool on) {
        micMuted = on;
        voiceChat->setMicMuted(on);
    });

    connect(btnDeafen, &QPushButton::toggled, this, [this](bool on) {
        audioMuted = on;
        voiceChat->setAudioMuted(on);
    });

    connect(btnLeave, &QPushButton::clicked, this, [this]() {
        voiceChat->disconnectFromServer();
        voipTree->clear();
        currentVoiceRoom.clear();
        outputPane->appendPlainText("[VoIP] Left room");
    });

    connect(voiceChat, &VoiceChat::voipKicked, this, [this]() {
        voipTree->clear();
        currentVoiceRoom.clear();
        voipStatusLabel->setText("Kicked from room");
        outputPane->appendPlainText("[VoIP] You were kicked from the room");
    });

    voipTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(voipTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = voipTree->itemAt(pos);
        if (!item || !item->parent())
            return;

        const int peerId = item->data(0, Qt::UserRole).toInt();
        if (peerId == voiceChat->id())
            return;

        QMenu menu(voipTree);

        const bool muted = voiceChat->isPeerMuted(peerId);
        menu.addAction(muted ? "Unmute" : "Mute for me", [this, peerId, muted]() {
            voiceChat->setPeerMuted(peerId, !muted);
            voipStatusLabel->setText(muted ? "Unmuted peer" : "Muted peer locally");
        });

        menu.addSeparator();

        auto *volWidget = new QWidget;
        auto *volLayout = new QHBoxLayout(volWidget);
        volLayout->setContentsMargins(8, 4, 8, 4);
        volLayout->addWidget(new QLabel("Volume:"));
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 200);
        slider->setValue(qRound(voiceChat->peerVolume(peerId) * 100));
        slider->setFixedWidth(120);
        connect(slider, &QSlider::valueChanged, this, [this, peerId](int val) {
            voiceChat->setPeerVolume(peerId, val / 100.0f);
        });
        volLayout->addWidget(slider);
        auto *volAction = new QWidgetAction(&menu);
        volAction->setDefaultWidget(volWidget);
        menu.addAction(volAction);

        menu.addSeparator();

        const QString currentNick = voipNicknames.value(peerId, QString::number(peerId));
        menu.addAction("Set nickname", [this, peerId, currentNick, item]() {
            bool ok;
            const QString nick = QInputDialog::getText(this,
                                                       "Set Nickname",
                                                       "Nickname:",
                                                       QLineEdit::Normal,
                                                       currentNick,
                                                       &ok);
            if (!ok)
                return;
            if (nick.isEmpty())
                voipNicknames.remove(peerId);
            else
                voipNicknames[peerId] = nick;
            item->setText(0, u8"\U0001F464 " + voipNicknames.value(peerId, QString::number(peerId)));
        });

        menu.addAction("Copy ID",
                       [peerId]() { QApplication::clipboard()->setText(QString::number(peerId)); });

        if (voiceChat->isHost()) {
            menu.addSeparator();
            menu.addAction("Kick from room", [this, peerId]() { voiceChat->kickPeer(peerId); });
        }

        menu.exec(voipTree->viewport()->mapToGlobal(pos));
    });
}

void MainWindow::addRoom(const QString &room)
{
    for (int i = 0; i < voipTree->topLevelItemCount(); i++) {
        if (voipTree->topLevelItem(i)->data(0, Qt::UserRole).toString() == room)
            return;
    }

    auto *roomItem = new QTreeWidgetItem(voipTree);
    roomItem->setText(0, u8"\U0001F50A " + room);
    roomItem->setData(0, Qt::UserRole, room);
    roomItem->setExpanded(true);
}

void MainWindow::onSessionProjectInit(int /*hostSiteId*/, const QStringList &files)
{
    const bool readOnly = session && session->collabMode() == CollabSession::Mode::ReadOnly;
    outputPane->appendPlainText(QString("[Collab] Project received — %1 file(s)%2")
                                    .arg(files.size())
                                    .arg(readOnly ? " (read-only)" : ""));

    if (collabStatusLabel) {
        collabStatusLabel->setText(
            QString("Guest — %1 file(s)%2").arg(files.size()).arg(readOnly ? " · read-only" : ""));
    }

    fileBrowser->setRemoteFiles(files);
    setSidePanelPage(0);
    btnFiles->setChecked(true);
}

void MainWindow::onSessionRunOutput(const QString &text)
{
    outputPane->appendPlainText(text);
    bottomTabs->setCurrentWidget(outputPane);
    bottomDock->setVisible(true);
}

void MainWindow::onSessionFileCreated(const QString &relPath)
{
    outputPane->appendPlainText("[Collab] File created: " + relPath);

    if (session && session->role() == CollabSession::Role::Guest)
        fileBrowser->setRemoteFiles(session->fileList());
}

void MainWindow::onSessionFileDeleted(const QString &relPath)
{
    outputPane->appendPlainText("[Collab] File deleted: " + relPath);
    if (session && session->role() == CollabSession::Role::Guest)
        fileBrowser->setRemoteFiles(session->fileList());
}

void MainWindow::onSessionFileRenamed(const QString &oldPath, const QString &newPath)
{
    outputPane->appendPlainText(QString("[Collab] File renamed: %1 → %2").arg(oldPath, newPath));
    if (session && session->role() == CollabSession::Role::Guest)
        fileBrowser->setRemoteFiles(session->fileList());
}

void MainWindow::joinRoom(const QString &room)
{
    if (joiningVoiceRoom)
        return;

    if (currentVoiceRoom == room && voiceChat->isConnected()) {
        return;
    }

    joiningVoiceRoom = true;
    currentVoiceRoom = room;

    disconnect(voiceChat, &VoiceChat::connectedToServer, this, nullptr);

    connect(
        voiceChat,
        &VoiceChat::connectedToServer,
        this,
        [this]() {
            if (!voiceChat->isCallActive()) {
                voiceChat->startCall();
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

bool MainWindow::showStartCollabDialog()
{
    const QString projectRoot = fileBrowser->rootPath();

    QDialog dlg(this);
    dlg.setWindowTitle("Start Collaboration");
    dlg.setMinimumWidth(400);
    auto *mainVl = new QVBoxLayout(&dlg);
    mainVl->setSpacing(10);

    auto *roomRow = new QHBoxLayout;
    roomRow->addWidget(new QLabel("Room name:"));
    auto *roomEdit = new QLineEdit;
    roomEdit->setPlaceholderText("e.g. my-project");
    roomRow->addWidget(roomEdit);
    mainVl->addLayout(roomRow);

    auto *permGroup = new QGroupBox("Guest permissions");
    auto *permHl = new QHBoxLayout(permGroup);
    auto *rbReadWrite = new QRadioButton("Read && Write");
    auto *rbReadOnly = new QRadioButton("Read-only");
    rbReadWrite->setChecked(true);
    permHl->addWidget(rbReadWrite);
    permHl->addWidget(rbReadOnly);
    mainVl->addWidget(permGroup);

    auto *filesGroup = new QGroupBox("Files to share");
    auto *filesVl = new QVBoxLayout(filesGroup);
    auto *rbAllFiles = new QRadioButton("All files");
    auto *rbSelFiles = new QRadioButton("Select files/folders");
    rbAllFiles->setChecked(true);
    filesVl->addWidget(rbAllFiles);
    filesVl->addWidget(rbSelFiles);

    auto *fileTree = new QTreeWidget;
    fileTree->setHeaderHidden(true);
    fileTree->setMinimumHeight(180);
    fileTree->setVisible(false);
    fileTree->setSelectionMode(QAbstractItemView::NoSelection);

    if (!projectRoot.isEmpty()) {
        std::function<void(QTreeWidgetItem *, const QString &, const QString &)> populate;
        populate = [&populate](QTreeWidgetItem *parent,
                               const QString &absPath,
                               const QString &relPath) {
            const QStringList filters = {"*.py", "*.cpp", "*.h", "*.pro", "*.txt", "*.md", "*.json"};
            QDir dir(absPath);
            for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                const QString subRel = relPath.isEmpty() ? sub : relPath + "/" + sub;
                auto *item = new QTreeWidgetItem(parent, {sub});
                item->setCheckState(0, Qt::Checked);
                populate(item, absPath + "/" + sub, subRel);
            }
            for (const QString &file : dir.entryList(filters, QDir::Files, QDir::Name)) {
                const QString fileRel = relPath.isEmpty() ? file : relPath + "/" + file;
                auto *item = new QTreeWidgetItem(parent, {file});
                item->setCheckState(0, Qt::Checked);
                item->setData(0, Qt::UserRole, fileRel);
            }
        };

        auto *rootItem = new QTreeWidgetItem(fileTree, {QFileInfo(projectRoot).fileName()});
        rootItem->setCheckState(0, Qt::Checked);
        fileTree->addTopLevelItem(rootItem);
        populate(rootItem, projectRoot, "");
        rootItem->setExpanded(true);

        connect(fileTree,
                &QTreeWidget::itemChanged,
                fileTree,
                [fileTree](QTreeWidgetItem *item, int col) {
                    if (col != 0 || !item->data(0, Qt::UserRole).toString().isEmpty())
                        return;
                    fileTree->blockSignals(true);
                    std::function<void(QTreeWidgetItem *, Qt::CheckState)> cascade;
                    cascade = [&cascade](QTreeWidgetItem *p, Qt::CheckState s) {
                        for (int i = 0; i < p->childCount(); ++i) {
                            p->child(i)->setCheckState(0, s);
                            cascade(p->child(i), s);
                        }
                    };
                    cascade(item,
                            item->checkState(0) != Qt::Unchecked ? Qt::Checked : Qt::Unchecked);
                    fileTree->blockSignals(false);
                });
    } else {
        rbSelFiles->setEnabled(false);
    }

    filesVl->addWidget(fileTree);
    connect(rbSelFiles, &QRadioButton::toggled, fileTree, &QWidget::setVisible);
    mainVl->addWidget(filesGroup);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *btnCancel = new QPushButton("Cancel");
    auto *btnStart = new QPushButton("Start");
    btnStart->setDefault(true);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnStart);
    mainVl->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnStart, &QPushButton::clicked, &dlg, [&dlg, roomEdit]() {
        if (!roomEdit->text().trimmed().isEmpty())
            dlg.accept();
    });

    if (dlg.exec() != QDialog::Accepted)
        return false;

    const QString room = roomEdit->text().trimmed();
    const CollabSession::Mode mode = rbReadOnly->isChecked() ? CollabSession::Mode::ReadOnly
                                                             : CollabSession::Mode::ReadWrite;

    QStringList selectedFiles;
    if (rbSelFiles->isChecked()) {
        std::function<void(QTreeWidgetItem *)> collect;
        collect = [&collect, &selectedFiles](QTreeWidgetItem *item) {
            const QString rel = item->data(0, Qt::UserRole).toString();
            if (!rel.isEmpty() && item->checkState(0) == Qt::Checked)
                selectedFiles.append(rel);
            for (int i = 0; i < item->childCount(); ++i)
                collect(item->child(i));
        };
        for (int i = 0; i < fileTree->topLevelItemCount(); ++i)
            collect(fileTree->topLevelItem(i));
    }

    startCollab(room, mode, selectedFiles);
    return true;
}

void MainWindow::onCollabUserContextMenu(const QPoint &pos)
{
    if (!session)
        return;

    QListWidgetItem *item = collabUsersList->itemAt(pos);
    if (!item)
        return;

    const int targetSiteId = item->data(Qt::UserRole).toInt();
    if (targetSiteId == session->siteId())
        return;

    QMenu menu(this);

    const QString relPath = peerFiles.value(targetSiteId);
    const QString peerName = peerNames.value(targetSiteId, QString("user_%1").arg(targetSiteId));
    QAction *gotoAct = menu.addAction(QString("Перейти до %1").arg(peerName));
    gotoAct->setEnabled(!relPath.isEmpty());
    connect(gotoAct, &QAction::triggered, this, [this, targetSiteId, relPath]() {
        if (!session || relPath.isEmpty())
            return;

        const bool isGuest = session->role() == CollabSession::Role::Guest;
        const QString openPath = isGuest ? relPath
                                         : QDir(session->projectRoot()).absoluteFilePath(relPath);

        setSidePanelPage(0);

        if (!isGuest)
            fileBrowser->revealFile(openPath);

        openFileFromBrowser(openPath);

        CodeEditor *targetEd = qobject_cast<CodeEditor *>(editorTabs->currentWidget());
        if (targetEd) {
            const int scintillaPos = targetEd->remoteCursorPos(targetSiteId);
            if (scintillaPos >= 0)
                targetEd->goToScintillaPos(scintillaPos);
        }
    });

    if (session->role() == CollabSession::Role::Host) {
        menu.addSeparator();
        QAction *kickAct = menu.addAction(QString("Kick %1").arg(peerName));
        connect(kickAct, &QAction::triggered, this, [this, targetSiteId, peerName]() {
            if (!session)
                return;
            session->kickUser(targetSiteId);
            outputPane->appendPlainText(QString("[Collab] Kicked %1").arg(peerName));
        });
    }

    menu.exec(collabUsersList->viewport()->mapToGlobal(pos));
}

void MainWindow::onEndCollabRequested()
{
    if (!session) {
        stopAllCollab();
        return;
    }
    pendingEndCollab = true;
    session->sendFinalStates(collectCurrentFileTexts());
    session->endSession();
    QTimer::singleShot(3000, this, [this]() {
        if (pendingEndCollab && (!reportDialog || !reportDialog->isVisible()))
            stopAllCollab();
    });
}

void MainWindow::onSessionReportReady(const SessionReportData &report)
{
    if (reportDialog) {
        reportDialog->close();
        reportDialog->deleteLater();
        reportDialog = nullptr;
    }

    pendingEndCollab = true;
    reportDialog = new SessionReportDialog(report, true, this);

    connect(reportDialog, &SessionReportDialog::sessionEnded, this, &MainWindow::stopAllCollab);
    connect(reportDialog, &QDialog::rejected, this, &MainWindow::stopAllCollab);

    connect(reportDialog, &QDialog::finished, this, [this]() {
        pendingEndCollab = false;
        reportDialog = nullptr;
    });

    reportDialog->show();
    outputPane->appendPlainText("[Collab] Session report received.");
}

void MainWindow::onSessionAiInsightsReady(const AiInsights &ai)
{
    if (reportDialog)
        reportDialog->updateAiSection(ai);
}

/* ── Auth ────────────────────────────────────────────────────── */

void MainWindow::setupAuthManager()
{
    auth = new AuthManager(this);
    auth->loadSavedSession();

    connect(auth, &AuthManager::sessionRestored, this, [this](const AuthManager::UserInfo &) {
        updateProfileButton();
    });
    connect(auth, &AuthManager::loginSuccess, this, [this](const AuthManager::UserInfo &) {
        updateProfileButton();
    });
    connect(auth, &AuthManager::logoutFinished, this, [this] { updateProfileButton(); });
}

void MainWindow::updateProfileButton()
{
    if (!btnProfile)
        return;

    const bool loggedIn = auth && auth->isLoggedIn();
    if (loggedIn) {
        const QString uname = auth->currentUser().username;
        const QString letter = uname.isEmpty() ? QStringLiteral("?")
                                               : QString(uname.at(0).toUpper());
        btnProfile->setText(letter);
        btnProfile->setToolTip(uname + "\n\nClick to sign out");
        btnProfile->setProperty("loggedIn", true);
    } else {
        btnProfile->setText("Account");
        btnProfile->setToolTip("Sign in");
        btnProfile->setProperty("loggedIn", false);
    }
    btnProfile->style()->unpolish(btnProfile);
    btnProfile->style()->polish(btnProfile);
}