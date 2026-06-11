#ifndef GITMANAGER_H
#define GITMANAGER_H

#include <QList>
#include <QObject>
#include <QString>

struct git_repository;
struct git_diff;

class GitManager : public QObject
{
    Q_OBJECT
public:
    struct FileStatus {
        QString path;
        QString oldPath;
        enum class State { Modified, Added, Deleted, Untracked, Renamed } state;
        bool staged = false;
    };

    struct CommitInfo {
        QString shortHash;
        QString fullHash;
        QString message;
        QString author;
        QString date;
    };

    explicit GitManager(QObject *parent = nullptr);
    ~GitManager() override;

    bool openRepo(const QString &path);
    bool initRepo(const QString &path);
    void closeRepo();
    bool isValid() const { return repo != nullptr; }
    QString workdir() const;

    QList<FileStatus> status();

    bool stageFile(const QString &relPath);
    bool unstageFile(const QString &relPath);
    bool stageAll();
    bool unstageAll();
    bool discardChanges(const QString &relPath);

    QString diffUnstaged(const QString &relPath = {});
    QString diffStaged(const QString &relPath = {});

    bool commit(const QString &message);
    int stagedCount();

    QString currentBranch();
    QStringList localBranches();
    bool checkoutBranch(const QString &name);
    bool createBranch(const QString &name, bool checkout = true);
    bool deleteBranch(const QString &name);

    QList<CommitInfo> log(int limit = 100);
    QString commitDiff(const QString &fullHash);

    QString lastError() const { return errorMsg; }

signals:
    void statusChanged();

private:
    git_repository *repo = nullptr;
    QString errorMsg;

    void setError(const QString &ctx);
    bool hasHead() const;
    QString printDiff(git_diff *diff);
};

#endif // GITMANAGER_H
