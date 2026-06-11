#ifndef GITPANEL_H
#define GITPANEL_H

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTreeWidget>
#include <QWidget>

#include "gitmanager.h"

class GitPanel : public QWidget
{
    Q_OBJECT
public:
    explicit GitPanel(QWidget *parent = nullptr);

    GitManager *manager() const { return git; }

    void setRepoPath(const QString &path);
    void refresh();
    void stageAll();
    void unstageAll();
    void pull();
    void push();
    void showHistory();
    void focusCommitMessage();
    void initRepo(const QString &path);

signals:
    void logMessage(const QString &msg);

private slots:
    void onFileClicked(QTreeWidgetItem *item, int col);
    void onFileDoubleClicked(QTreeWidgetItem *item, int col);
    void onContextMenu(const QPoint &pos);
    void onCommitClicked();
    void onBranchActivated(int index);
    void onNewBranch();

private:
    GitManager *git;
    QString repoPath;
    bool updatingBranches = false;

    // Top bar
    QComboBox *branchCombo;
    QPushButton *btnRefresh;
    QPushButton *btnPull;
    QPushButton *btnPush;
    QPushButton *btnHistory;

    // File tree
    QTreeWidget *fileTree;
    QTreeWidgetItem *stagedHeader;
    QTreeWidgetItem *unstagedHeader;

    // Diff
    QTextEdit *diffView;

    // Bottom bar
    QPushButton *btnStageAll;
    QPushButton *btnUnstageAll;
    QLineEdit *commitMsg;
    QPushButton *btnCommit;

    QWidget *noRepoPane;
    QWidget *repoPane;

    void setupUi();
    void populateTree(const QList<GitManager::FileStatus> &statusList);
    void showDiff(const QString &path, bool staged);
    static void applyDiff(QTextEdit *view, const QString &raw);
    static QString stateLabel(GitManager::FileStatus::State state);
    void runGitProcess(const QString &op, const QStringList &args);
    void setRepoAvailable(bool on);
};

#endif // GITPANEL_H
