#include "gitpanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSplitter>
#include <QVBoxLayout>

static const char *NEW_BRANCH_ITEM = "__new__";

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
    , git(new GitManager(this))
{
    setupUi();
    connect(git, &GitManager::statusChanged, this, &GitPanel::refresh);
}

void GitPanel::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    noRepoPane = new QWidget;
    {
        auto *vl = new QVBoxLayout(noRepoPane);
        vl->addStretch();
        auto *lbl = new QLabel("Not a git repository.\n\nOpen a folder that contains a .git "
                               "directory,\nor initialize a new repository.");
        lbl->setObjectName("stubLabel");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setWordWrap(true);
        vl->addWidget(lbl);

        auto *btnInit = new QPushButton("Initialize Repository");
        btnInit->setObjectName("voipBtn");
        btnInit->setMaximumWidth(200);
        connect(btnInit, &QPushButton::clicked, this, [this]() {
            if (!repoPath.isEmpty())
                initRepo(repoPath);
        });
        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch();
        btnRow->addWidget(btnInit);
        btnRow->addStretch();
        vl->addLayout(btnRow);
        vl->addStretch();
    }
    root->addWidget(noRepoPane);

    repoPane = new QWidget;
    auto *repoVl = new QVBoxLayout(repoPane);
    repoVl->setContentsMargins(0, 0, 0, 0);
    repoVl->setSpacing(0);

    auto *topBar = new QWidget;
    topBar->setObjectName("gitTopBar");
    auto *topH = new QHBoxLayout(topBar);
    topH->setContentsMargins(6, 4, 6, 4);
    topH->setSpacing(4);

    branchCombo = new QComboBox;
    branchCombo->setMinimumWidth(110);
    branchCombo->setMaximumWidth(200);
    topH->addWidget(branchCombo);

    btnRefresh = new QPushButton("Refresh");
    btnRefresh->setToolTip("Refresh status  (Ctrl+Shift+G)");
    btnRefresh->setFixedHeight(26);
    btnRefresh->setObjectName("voipBtn");
    topH->addWidget(btnRefresh);

    topH->addStretch();

    btnPull = new QPushButton("Pull");
    btnPush = new QPushButton("Push");
    for (auto *b : {btnPull, btnPush})
        b->setObjectName("voipBtn");
    topH->addWidget(btnPull);
    topH->addWidget(btnPush);

    repoVl->addWidget(topBar);

    auto *topSep = new QFrame;
    topSep->setFrameShape(QFrame::HLine);
    topSep->setObjectName("panelSeparator");
    repoVl->addWidget(topSep);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setObjectName("gitSplitter");

    auto *leftWidget = new QWidget;
    auto *leftVl = new QVBoxLayout(leftWidget);
    leftVl->setContentsMargins(0, 0, 0, 0);
    leftVl->setSpacing(0);

    auto *changesLabel = new QLabel("  LOCAL CHANGES");
    changesLabel->setObjectName("gitSectionHeader");
    QFont sectionFont;
    sectionFont.setBold(true);
    sectionFont.setPointSize(sectionFont.pointSize() - 1);
    changesLabel->setFont(sectionFont);
    changesLabel->setFixedHeight(22);
    leftVl->addWidget(changesLabel);

    fileTree = new QTreeWidget;
    fileTree->setHeaderHidden(true);
    fileTree->setObjectName("fileTree");
    fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    fileTree->setRootIsDecorated(true);
    fileTree->setIndentation(14);
    leftVl->addWidget(fileTree, 1);

    QFont hf;
    hf.setBold(true);
    hf.setPointSize(hf.pointSize() - 1);

    stagedHeader = new QTreeWidgetItem(fileTree, {"STAGED (0)"});
    stagedHeader->setFont(0, hf);
    stagedHeader->setForeground(0, QColor("#6db96d"));
    stagedHeader->setExpanded(true);

    unstagedHeader = new QTreeWidgetItem(fileTree, {"UNSTAGED (0)"});
    unstagedHeader->setFont(0, hf);
    unstagedHeader->setForeground(0, QColor("#c97070"));
    unstagedHeader->setExpanded(true);

    auto *commitSep = new QFrame;
    commitSep->setFrameShape(QFrame::HLine);
    commitSep->setObjectName("panelSeparator");
    leftVl->addWidget(commitSep);

    auto *stageRow = new QHBoxLayout;
    stageRow->setContentsMargins(4, 4, 4, 2);
    stageRow->setSpacing(4);
    btnStageAll = new QPushButton("Stage All");
    btnUnstageAll = new QPushButton("Unstage All");
    for (auto *b : {btnStageAll, btnUnstageAll})
        b->setObjectName("voipBtn");
    stageRow->addWidget(btnStageAll);
    stageRow->addWidget(btnUnstageAll);
    stageRow->addStretch();
    leftVl->addLayout(stageRow);

    auto *commitRow = new QHBoxLayout;
    commitRow->setContentsMargins(4, 2, 4, 6);
    commitRow->setSpacing(4);
    commitMsg = new QLineEdit;
    commitMsg->setPlaceholderText("Commit message…");
    btnCommit = new QPushButton("Commit");
    btnCommit->setObjectName("voipBtn");
    btnCommit->setEnabled(false);
    commitRow->addWidget(commitMsg, 1);
    commitRow->addWidget(btnCommit);
    leftVl->addLayout(commitRow);

    splitter->addWidget(leftWidget);

    auto *rightWidget = new QWidget;
    auto *rightVl = new QVBoxLayout(rightWidget);
    rightVl->setContentsMargins(0, 0, 0, 0);
    rightVl->setSpacing(0);

    auto *logLabel = new QLabel("  LOG");
    logLabel->setObjectName("gitSectionHeader");
    logLabel->setFont(sectionFont);
    logLabel->setFixedHeight(22);
    rightVl->addWidget(logLabel);

    logTree = new QTreeWidget;
    logTree->setObjectName("gitLogTree");
    logTree->setColumnCount(4);
    logTree->setHeaderLabels({"Message", "Author", "Date", "Hash"});
    logTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    logTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    logTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    logTree->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    logTree->header()->resizeSection(3, 64);
    logTree->setRootIsDecorated(false);
    logTree->setAlternatingRowColors(true);
    logTree->setSelectionMode(QAbstractItemView::SingleSelection);
    logTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTree->setIndentation(0);
    logTree->setUniformRowHeights(true);
    rightVl->addWidget(logTree, 1);

    splitter->addWidget(rightWidget);
    splitter->setSizes({220, 460});

    repoVl->addWidget(splitter, 1);
    root->addWidget(repoPane);

    noRepoPane->show();
    repoPane->hide();

    connect(fileTree, &QTreeWidget::itemClicked, this, &GitPanel::onFileClicked);
    connect(fileTree, &QTreeWidget::itemDoubleClicked, this, &GitPanel::onFileDoubleClicked);
    connect(fileTree, &QTreeWidget::customContextMenuRequested, this, &GitPanel::onContextMenu);
    connect(branchCombo,
            QOverload<int>::of(&QComboBox::activated),
            this,
            &GitPanel::onBranchActivated);
    connect(btnRefresh, &QPushButton::clicked, this, &GitPanel::refresh);
    connect(btnPull, &QPushButton::clicked, this, &GitPanel::pull);
    connect(btnPush, &QPushButton::clicked, this, &GitPanel::push);
    connect(btnStageAll, &QPushButton::clicked, this, [this]() { git->stageAll(); });
    connect(btnUnstageAll, &QPushButton::clicked, this, [this]() { git->unstageAll(); });
    connect(btnCommit, &QPushButton::clicked, this, &GitPanel::onCommitClicked);
    connect(commitMsg, &QLineEdit::textChanged, this, [this](const QString &t) {
        btnCommit->setEnabled(!t.trimmed().isEmpty() && git->stagedCount() > 0);
    });
}

void GitPanel::setRepoPath(const QString &path)
{
    repoPath = path;
    if (git->openRepo(path)) {
        setRepoAvailable(true);
        refresh();
    } else {
        setRepoAvailable(false);
    }
}

void GitPanel::initRepo(const QString &path)
{
    if (!git->initRepo(path)) {
        QMessageBox::warning(this, "Git Init", git->lastError());
        return;
    }
    repoPath = path;
    setRepoAvailable(true);
    refresh();
    emit logMessage("[Git] Initialized empty repository in " + path);
}

void GitPanel::setRepoAvailable(bool on)
{
    noRepoPane->setVisible(!on);
    repoPane->setVisible(on);
}

void GitPanel::refresh()
{
    if (!git->isValid())
        return;

    updatingBranches = true;
    branchCombo->clear();
    const QString current = git->currentBranch();
    for (const QString &b : git->localBranches())
        branchCombo->addItem(b);
    branchCombo->addItem("New Branch…", NEW_BRANCH_ITEM);
    branchCombo->setCurrentText(current);
    updatingBranches = false;

    populateTree(git->status());

    const int staged = git->stagedCount();
    btnCommit->setEnabled(staged > 0 && !commitMsg->text().trimmed().isEmpty());

    refreshLog();
}

void GitPanel::refreshLog()
{
    logTree->clear();
    const auto commits = git->log(200);
    for (const auto &c : commits) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, "● " + c.message);
        item->setText(1, c.author);
        item->setText(2, c.date);
        item->setText(3, c.shortHash);
        item->setForeground(0, QColor("#cccccc"));
        item->setForeground(1, QColor("#9cdcfe"));
        item->setForeground(2, QColor("#858585"));
        item->setForeground(3, QColor("#569cd6"));
        item->setData(0, Qt::UserRole, c.fullHash);
        item->setToolTip(0, c.message);
        logTree->addTopLevelItem(item);
    }
}

void GitPanel::stageAll()
{
    git->stageAll();
}
void GitPanel::unstageAll()
{
    git->unstageAll();
}

void GitPanel::focusCommitMessage()
{
    if (repoPane->isVisible())
        commitMsg->setFocus();
}

void GitPanel::populateTree(const QList<GitManager::FileStatus> &statusList)
{
    qDeleteAll(stagedHeader->takeChildren());
    qDeleteAll(unstagedHeader->takeChildren());

    int nStaged = 0, nUnstaged = 0;

    for (const auto &s : statusList) {
        QString label = stateLabel(s.state) + "  " + s.path;
        if (!s.oldPath.isEmpty())
            label += "  ←  " + s.oldPath;

        auto *item = new QTreeWidgetItem(s.staged ? stagedHeader : unstagedHeader, {label});
        item->setData(0, Qt::UserRole, s.path);
        item->setData(0, Qt::UserRole + 1, s.staged);

        if (s.staged) {
            item->setForeground(0, QColor("#6db96d"));
            ++nStaged;
        } else {
            item->setForeground(0,
                                s.state == GitManager::FileStatus::State::Untracked
                                    ? QColor("#858585")
                                    : QColor("#c97070"));
            ++nUnstaged;
        }
    }

    stagedHeader->setText(0, QString("STAGED (%1)").arg(nStaged));
    unstagedHeader->setText(0, QString("UNSTAGED (%1)").arg(nUnstaged));
    stagedHeader->setExpanded(true);
    unstagedHeader->setExpanded(true);
}

void GitPanel::onFileClicked(QTreeWidgetItem *item, int)
{
    if (!item || !item->parent())
        return;
    emit diffRequested(item->data(0, Qt::UserRole).toString(),
                       item->data(0, Qt::UserRole + 1).toBool());
}

void GitPanel::onFileDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item || !item->parent())
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    const bool staged = item->data(0, Qt::UserRole + 1).toBool();
    if (staged)
        git->unstageFile(path);
    else
        git->stageFile(path);
}

void GitPanel::onContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = fileTree->itemAt(pos);
    if (!item || !item->parent())
        return;

    const QString path = item->data(0, Qt::UserRole).toString();
    const bool staged = item->data(0, Qt::UserRole + 1).toBool();

    QMenu menu(this);

    if (staged) {
        menu.addAction("Unstage", this, [this, path]() { git->unstageFile(path); });
    } else {
        menu.addAction("Stage", this, [this, path]() { git->stageFile(path); });
        menu.addSeparator();
        menu.addAction("Discard Changes", this, [this, path]() {
            const auto btn = QMessageBox::question(
                this,
                "Discard Changes",
                QString("Discard all local changes in:\n%1\n\nThis cannot be undone.").arg(path),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (btn == QMessageBox::Yes)
                git->discardChanges(path);
        });
    }
    menu.addSeparator();
    menu.addAction("Show Diff", this, [this, path, staged]() { emit diffRequested(path, staged); });

    menu.exec(fileTree->viewport()->mapToGlobal(pos));
}

void GitPanel::onCommitClicked()
{
    const QString msg = commitMsg->text().trimmed();
    if (msg.isEmpty())
        return;

    if (!git->commit(msg)) {
        QMessageBox::warning(this, "Commit Failed", git->lastError());
        return;
    }
    commitMsg->clear();
    emit logMessage("[Git] Committed: " + msg);
    refresh();
}

void GitPanel::onBranchActivated(int index)
{
    if (updatingBranches)
        return;

    const QString data = branchCombo->itemData(index).toString();
    if (data == NEW_BRANCH_ITEM) {
        onNewBranch();
        return;
    }

    const QString name = branchCombo->itemText(index);
    if (name == git->currentBranch())
        return;

    if (git->stagedCount() > 0) {
        QMessageBox::warning(
            this,
            "Switch Branch",
            "Cannot switch branches with staged changes.\nCommit or unstage your changes first.");
        updatingBranches = true;
        branchCombo->setCurrentText(git->currentBranch());
        updatingBranches = false;
        return;
    }

    if (!git->checkoutBranch(name)) {
        QMessageBox::warning(this, "Checkout Failed", git->lastError());
        updatingBranches = true;
        branchCombo->setCurrentText(git->currentBranch());
        updatingBranches = false;
    } else {
        emit logMessage("[Git] Switched to branch: " + name);
        refresh();
    }
}

void GitPanel::onNewBranch()
{
    bool ok = false;
    const QString name
        = QInputDialog::getText(this, "New Branch", "Branch name:", QLineEdit::Normal, {}, &ok)
              .trimmed();

    if (!ok || name.isEmpty()) {
        updatingBranches = true;
        branchCombo->setCurrentText(git->currentBranch());
        updatingBranches = false;
        return;
    }

    if (!git->createBranch(name, true))
        QMessageBox::warning(this, "Create Branch Failed", git->lastError());
    else
        emit logMessage("[Git] Created and switched to branch: " + name);
    refresh();
}

void GitPanel::runGitProcess(const QString &op, const QStringList &args)
{
    const QString wd = git->workdir();
    if (wd.isEmpty())
        return;

    emit logMessage(QString("[Git] Running: git %1").arg(op));

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(wd);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        emit logMessage(QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed());
    });
    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, proc, op](int code, QProcess::ExitStatus) {
                emit logMessage(QString("[Git] %1 exit code: %2").arg(op).arg(code));
                proc->deleteLater();
                refresh();
            });

    proc->start("git", args);
}

void GitPanel::pull()
{
    runGitProcess("pull", {"pull"});
}
void GitPanel::push()
{
    runGitProcess("push", {"push"});
}

QString GitPanel::stateLabel(GitManager::FileStatus::State state)
{
    switch (state) {
    case GitManager::FileStatus::State::Modified:
        return "M";
    case GitManager::FileStatus::State::Added:
        return "A";
    case GitManager::FileStatus::State::Deleted:
        return "D";
    case GitManager::FileStatus::State::Untracked:
        return "?";
    case GitManager::FileStatus::State::Renamed:
        return "R";
    }
    return "?";
}
