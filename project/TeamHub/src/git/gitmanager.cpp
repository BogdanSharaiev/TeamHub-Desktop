#include "gitmanager.h"

#include <QDateTime>
#include <QSet>
#include <git2.h>

static int diffLineCb(const git_diff_delta *,
                      const git_diff_hunk *,
                      const git_diff_line *line,
                      void *payload)
{
    const char o = line->origin;
    if (o == '+' || o == '-' || o == ' ')
        static_cast<QString *>(payload)->append(QChar(o));
    static_cast<QString *>(payload)->append(
        QString::fromUtf8(line->content, static_cast<int>(line->content_len)));
    return 0;
}

GitManager::GitManager(QObject *parent)
    : QObject(parent)
{
    git_libgit2_init();
}

GitManager::~GitManager()
{
    closeRepo();
    git_libgit2_shutdown();
}

void GitManager::closeRepo()
{
    if (repo) {
        git_repository_free(repo);
        repo = nullptr;
    }
}

bool GitManager::openRepo(const QString &path)
{
    closeRepo();
    if (git_repository_open_ext(&repo, path.toUtf8(), 0, nullptr) != 0) {
        setError("open");
        repo = nullptr;
        return false;
    }
    return true;
}

bool GitManager::initRepo(const QString &path)
{
    closeRepo();
    if (git_repository_init(&repo, path.toUtf8(), 0) != 0) {
        setError("init");
        repo = nullptr;
        return false;
    }
    return true;
}

QString GitManager::workdir() const
{
    if (!repo)
        return {};
    const char *wd = git_repository_workdir(repo);
    return wd ? QString::fromUtf8(wd) : QString{};
}

bool GitManager::hasHead() const
{
    git_oid oid;
    return git_reference_name_to_id(&oid, repo, "HEAD") == 0;
}

void GitManager::setError(const QString &ctx)
{
    const git_error *e = git_error_last();
    errorMsg = ctx + ": " + (e ? QString::fromUtf8(e->message) : "unknown error");
}

QList<GitManager::FileStatus> GitManager::status()
{
    QList<FileStatus> result;
    if (!repo)
        return result;

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR
                 | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;

    git_status_list *list = nullptr;
    if (git_status_list_new(&list, repo, &opts) != 0)
        return result;

    const size_t count = git_status_list_entrycount(list);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry *e = git_status_byindex(list, i);
        if (!e || e->status == GIT_STATUS_CURRENT)
            continue;

        const unsigned staged_flags = GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED
                                      | GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED;
        if (e->status & staged_flags) {
            FileStatus s;
            s.staged = true;
            if (e->head_to_index) {
                if (e->head_to_index->new_file.path)
                    s.path = QString::fromUtf8(e->head_to_index->new_file.path);
                if (e->head_to_index->old_file.path) {
                    const QString old = QString::fromUtf8(e->head_to_index->old_file.path);
                    if (old != s.path)
                        s.oldPath = old;
                }
            }
            if (s.path.isEmpty() && e->index_to_workdir && e->index_to_workdir->new_file.path)
                s.path = QString::fromUtf8(e->index_to_workdir->new_file.path);

            if (e->status & GIT_STATUS_INDEX_NEW)
                s.state = FileStatus::State::Added;
            else if (e->status & GIT_STATUS_INDEX_DELETED)
                s.state = FileStatus::State::Deleted;
            else if (e->status & GIT_STATUS_INDEX_RENAMED)
                s.state = FileStatus::State::Renamed;
            else
                s.state = FileStatus::State::Modified;

            if (!s.path.isEmpty())
                result.append(s);
        }

        const unsigned wt_flags = GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_NEW
                                  | GIT_STATUS_WT_RENAMED;
        if (e->status & wt_flags) {
            FileStatus s;
            s.staged = false;
            if (e->index_to_workdir) {
                if (e->index_to_workdir->new_file.path)
                    s.path = QString::fromUtf8(e->index_to_workdir->new_file.path);
                if (e->index_to_workdir->old_file.path) {
                    const QString old = QString::fromUtf8(e->index_to_workdir->old_file.path);
                    if (old != s.path)
                        s.oldPath = old;
                }
            }
            if (e->status & GIT_STATUS_WT_NEW)
                s.state = FileStatus::State::Untracked;
            else if (e->status & GIT_STATUS_WT_DELETED)
                s.state = FileStatus::State::Deleted;
            else if (e->status & GIT_STATUS_WT_RENAMED)
                s.state = FileStatus::State::Renamed;
            else
                s.state = FileStatus::State::Modified;

            if (!s.path.isEmpty())
                result.append(s);
        }
    }
    git_status_list_free(list);
    return result;
}

bool GitManager::stageFile(const QString &relPath)
{
    if (!repo)
        return false;
    git_index *index = nullptr;
    git_repository_index(&index, repo);

    const int err = git_index_add_bypath(index, relPath.toUtf8());
    if (err != 0)
        setError("stage");
    else {
        git_index_write(index);
    }
    git_index_free(index);

    if (err != 0)
        return false;
    emit statusChanged();
    return true;
}

bool GitManager::unstageFile(const QString &relPath)
{
    if (!repo)
        return false;

    if (!hasHead()) {
        git_index *index = nullptr;
        git_repository_index(&index, repo);
        git_index_remove_bypath(index, relPath.toUtf8());
        git_index_write(index);
        git_index_free(index);
        emit statusChanged();
        return true;
    }

    git_object *head = nullptr;
    git_revparse_single(&head, repo, "HEAD");

    QByteArray pathBytes = relPath.toUtf8();
    char *pathPtr = pathBytes.data();
    git_strarray paths = {&pathPtr, 1};
    const int err = git_reset_default(repo, head, &paths);
    git_object_free(head);

    if (err != 0) {
        setError("unstage");
        return false;
    }
    emit statusChanged();
    return true;
}

bool GitManager::stageAll()
{
    if (!repo)
        return false;
    git_index *index = nullptr;
    git_repository_index(&index, repo);

    git_strarray empty = {nullptr, 0};
    const int err = git_index_add_all(index, &empty, 0, nullptr, nullptr);
    if (err == 0)
        git_index_write(index);
    git_index_free(index);

    if (err != 0) {
        setError("stage all");
        return false;
    }
    emit statusChanged();
    return true;
}

bool GitManager::unstageAll()
{
    if (!repo)
        return false;

    if (!hasHead()) {
        git_index *index = nullptr;
        git_repository_index(&index, repo);
        git_index_clear(index);
        git_index_write(index);
        git_index_free(index);
        emit statusChanged();
        return true;
    }

    git_object *head = nullptr;
    git_revparse_single(&head, repo, "HEAD");
    const int err = git_reset(repo, head, GIT_RESET_MIXED, nullptr);
    git_object_free(head);

    if (err != 0) {
        setError("unstage all");
        return false;
    }
    emit statusChanged();
    return true;
}

bool GitManager::discardChanges(const QString &relPath)
{
    if (!repo)
        return false;

    QByteArray pathBytes = relPath.toUtf8();
    char *pathPtr = pathBytes.data();
    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    opts.paths.strings = &pathPtr;
    opts.paths.count = 1;

    const int err = git_checkout_head(repo, &opts);
    if (err != 0) {
        setError("discard");
        return false;
    }
    emit statusChanged();
    return true;
}

QString GitManager::printDiff(git_diff *diff)
{
    if (!diff)
        return {};
    QString result;
    git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, diffLineCb, &result);
    git_diff_free(diff);
    return result;
}

GitManager::DiffLineStats GitManager::parseDiffStats(const QString &raw)
{
    QList<int> added;
    QSet<int> removedAt;
    QList<DiffLineStats::CharRange> changedRanges;

    int newLine = 0;
    bool inHunk = false;
    QStringList pendingDels;

    auto flushDels = [&]() {
        if (!pendingDels.isEmpty()) {
            removedAt.insert(qMax(1, newLine + 1));
            pendingDels.clear();
        }
    };

    for (const QString &ln : raw.split('\n')) {
        if (ln.startsWith("@@ ")) {
            flushDels();
            const int plusPos = ln.indexOf('+');
            if (plusPos != -1) {
                int end = plusPos + 1;
                while (end < ln.size() && ln[end].isDigit())
                    ++end;
                bool ok;
                const int start = ln.mid(plusPos + 1, end - plusPos - 1).toInt(&ok);
                if (ok)
                    newLine = start - 1;
            }
            inHunk = true;
            continue;
        }
        if (!inHunk || ln.isEmpty())
            continue;

        const QChar ch = ln[0];
        if (ch == '+' && !ln.startsWith("+++")) {
            ++newLine;
            added.append(newLine);
            const QString newText = ln.mid(1);
            if (!pendingDels.isEmpty()) {
                const QString oldText = pendingDels.takeFirst();
                int colStart = 0;
                const int minLen = qMin(oldText.size(), newText.size());
                while (colStart < minLen && oldText[colStart] == newText[colStart])
                    ++colStart;
                int oldSuf = 0, newSuf = 0;
                while (newSuf < newText.size() - colStart && oldSuf < oldText.size() - colStart
                       && oldText[oldText.size() - 1 - oldSuf]
                              == newText[newText.size() - 1 - newSuf]) {
                    ++oldSuf;
                    ++newSuf;
                }
                const int colEnd = newText.size() - newSuf;
                if (colStart < colEnd)
                    changedRanges.append({newLine, colStart, colEnd});
            }
        } else if (ch == '-' && !ln.startsWith("---")) {
            pendingDels.append(ln.mid(1));
        } else if (ch == ' ') {
            flushDels();
            ++newLine;
        }
    }
    flushDels();

    const QSet<int> addedSet(added.begin(), added.end());
    QList<int> cleanedRemovedAt;
    for (int r : removedAt)
        if (!addedSet.contains(r))
            cleanedRemovedAt.append(r);

    return {added, cleanedRemovedAt, changedRanges};
}

GitManager::DiffLineStats GitManager::diffLineStats(const QString &relPath, bool staged)
{
    if (!repo)
        return {};
    const QString raw = staged ? diffStaged(relPath) : diffUnstaged(relPath);
    return parseDiffStats(raw);
}

QString GitManager::diffUnstaged(const QString &relPath)
{
    if (!repo)
        return {};

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.flags |= GIT_DIFF_INCLUDE_UNTRACKED;

    QByteArray pathBytes;
    char *pathPtr = nullptr;
    if (!relPath.isEmpty()) {
        pathBytes = relPath.toUtf8();
        pathPtr = pathBytes.data();
        opts.pathspec.strings = &pathPtr;
        opts.pathspec.count = 1;
    }

    git_index *index = nullptr;
    git_repository_index(&index, repo);
    git_index_read(index, 0);

    git_diff *diff = nullptr;
    git_diff_index_to_workdir(&diff, repo, index, &opts);
    git_index_free(index);

    return printDiff(diff);
}

QString GitManager::diffStaged(const QString &relPath)
{
    if (!repo)
        return {};

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;

    QByteArray pathBytes;
    char *pathPtr = nullptr;
    if (!relPath.isEmpty()) {
        pathBytes = relPath.toUtf8();
        pathPtr = pathBytes.data();
        opts.pathspec.strings = &pathPtr;
        opts.pathspec.count = 1;
    }

    git_tree *headTree = nullptr;
    if (hasHead()) {
        git_oid headOid;
        git_reference_name_to_id(&headOid, repo, "HEAD");
        git_commit *headCommit = nullptr;
        git_commit_lookup(&headCommit, repo, &headOid);
        git_commit_tree(&headTree, headCommit);
        git_commit_free(headCommit);
    }

    git_diff *diff = nullptr;
    git_diff_tree_to_index(&diff, repo, headTree, nullptr, &opts);
    if (headTree)
        git_tree_free(headTree);

    return printDiff(diff);
}

int GitManager::stagedCount()
{
    if (!repo)
        return 0;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_ONLY;
    opts.flags = GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
    git_status_list *list = nullptr;
    if (git_status_list_new(&list, repo, &opts) != 0)
        return 0;
    const size_t count = git_status_list_entrycount(list);
    int n = 0;
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry *e = git_status_byindex(list, i);
        if (e && e->status != GIT_STATUS_CURRENT)
            ++n;
    }
    git_status_list_free(list);
    return n;
}

bool GitManager::commit(const QString &message)
{
    if (!repo)
        return false;

    git_index *index = nullptr;
    git_repository_index(&index, repo);
    git_index_read(index, 1);

    git_oid treeOid;
    if (git_index_write_tree(&treeOid, index) != 0) {
        setError("write tree");
        git_index_free(index);
        return false;
    }
    git_index_write(index);
    git_index_free(index);

    git_tree *tree = nullptr;
    if (git_tree_lookup(&tree, repo, &treeOid) != 0) {
        setError("tree lookup");
        return false;
    }

    git_signature *sig = nullptr;
    if (git_signature_default(&sig, repo) != 0)
        git_signature_now(&sig, "TeamHub User", "teamhub@local");

    git_commit *parent = nullptr;
    git_oid parentOid;
    const bool hasParent = (git_reference_name_to_id(&parentOid, repo, "HEAD") == 0);
    if (hasParent)
        git_commit_lookup(&parent, repo, &parentOid);

    git_oid newOid;
    const git_commit *parents[] = {parent};
    const int err = git_commit_create(&newOid,
                                      repo,
                                      "HEAD",
                                      sig,
                                      sig,
                                      "UTF-8",
                                      message.toUtf8(),
                                      tree,
                                      hasParent ? 1u : 0u,
                                      hasParent ? parents : nullptr);

    git_tree_free(tree);
    git_signature_free(sig);
    if (parent)
        git_commit_free(parent);

    if (err != 0) {
        setError("commit");
        return false;
    }
    emit statusChanged();
    return true;
}

QString GitManager::currentBranch()
{
    if (!repo)
        return {};
    git_reference *head = nullptr;
    if (git_repository_head(&head, repo) != 0)
        return "(unborn HEAD)";
    const char *name = nullptr;
    git_branch_name(&name, head);
    const QString result = name ? QString::fromUtf8(name) : QString("(detached)");
    git_reference_free(head);
    return result;
}

QStringList GitManager::localBranches()
{
    if (!repo)
        return {};
    QStringList result;
    git_branch_iterator *it = nullptr;
    if (git_branch_iterator_new(&it, repo, GIT_BRANCH_LOCAL) != 0)
        return result;

    git_reference *ref = nullptr;
    git_branch_t type;
    while (git_branch_next(&ref, &type, it) == 0) {
        const char *name = nullptr;
        git_branch_name(&name, ref);
        if (name)
            result.append(QString::fromUtf8(name));
        git_reference_free(ref);
    }
    git_branch_iterator_free(it);
    return result;
}

bool GitManager::checkoutBranch(const QString &name)
{
    if (!repo)
        return false;
    git_reference *ref = nullptr;
    if (git_branch_lookup(&ref, repo, name.toUtf8(), GIT_BRANCH_LOCAL) != 0) {
        setError("branch lookup");
        return false;
    }
    git_object *obj = nullptr;
    git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT);

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;

    int err = git_checkout_tree(repo, obj, &opts);
    if (err == 0)
        err = git_repository_set_head(repo, git_reference_name(ref));

    git_object_free(obj);
    git_reference_free(ref);

    if (err != 0) {
        setError("checkout");
        return false;
    }
    emit statusChanged();
    return true;
}

bool GitManager::createBranch(const QString &name, bool checkout)
{
    if (!repo || !hasHead())
        return false;

    git_oid headOid;
    if (git_reference_name_to_id(&headOid, repo, "HEAD") != 0) {
        setError("HEAD");
        return false;
    }
    git_commit *headCommit = nullptr;
    git_commit_lookup(&headCommit, repo, &headOid);

    git_reference *ref = nullptr;
    const int err = git_branch_create(&ref, repo, name.toUtf8(), headCommit, 0);
    git_commit_free(headCommit);
    if (err != 0) {
        setError("create branch");
        return false;
    }

    if (checkout) {
        git_object *obj = nullptr;
        git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT);
        git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
        opts.checkout_strategy = GIT_CHECKOUT_SAFE;
        git_checkout_tree(repo, obj, &opts);
        git_repository_set_head(repo, git_reference_name(ref));
        git_object_free(obj);
    }
    git_reference_free(ref);
    emit statusChanged();
    return true;
}

bool GitManager::deleteBranch(const QString &name)
{
    if (!repo)
        return false;
    git_reference *ref = nullptr;
    if (git_branch_lookup(&ref, repo, name.toUtf8(), GIT_BRANCH_LOCAL) != 0) {
        setError("branch lookup");
        return false;
    }
    const int err = git_branch_delete(ref);
    git_reference_free(ref);
    if (err != 0) {
        setError("delete branch");
        return false;
    }
    emit statusChanged();
    return true;
}

QList<GitManager::CommitInfo> GitManager::log(int limit)
{
    QList<CommitInfo> result;
    if (!repo || !hasHead())
        return result;

    git_revwalk *walk = nullptr;
    if (git_revwalk_new(&walk, repo) != 0)
        return result;
    git_revwalk_push_head(walk);
    git_revwalk_sorting(walk, GIT_SORT_TIME);

    git_oid oid;
    for (int i = 0; i < limit && git_revwalk_next(&oid, walk) == 0; ++i) {
        git_commit *c = nullptr;
        if (git_commit_lookup(&c, repo, &oid) != 0)
            continue;

        CommitInfo info;
        char shortBuf[9] = {}, fullBuf[41] = {};
        git_oid_tostr(shortBuf, sizeof(shortBuf), &oid);
        git_oid_tostr(fullBuf, sizeof(fullBuf), &oid);
        info.shortHash = QString::fromUtf8(shortBuf);
        info.fullHash = QString::fromUtf8(fullBuf);

        const char *msg = git_commit_message(c);
        info.message = msg ? QString::fromUtf8(msg).split('\n').first().trimmed() : QString{};

        const git_signature *author = git_commit_author(c);
        info.author = author ? QString::fromUtf8(author->name) : QString{};
        info.date = author ? QDateTime::fromSecsSinceEpoch(author->when.time)
                                 .toString("yyyy-MM-dd HH:mm")
                           : QString{};

        result.append(info);
        git_commit_free(c);
    }
    git_revwalk_free(walk);
    return result;
}

QString GitManager::commitDiff(const QString &fullHash)
{
    if (!repo)
        return {};

    git_oid oid;
    if (git_oid_fromstr(&oid, fullHash.toUtf8()) != 0)
        return {};

    git_commit *commit = nullptr;
    if (git_commit_lookup(&commit, repo, &oid) != 0)
        return {};

    git_tree *newTree = nullptr;
    git_commit_tree(&newTree, commit);

    git_tree *oldTree = nullptr;
    if (git_commit_parentcount(commit) > 0) {
        git_commit *parent = nullptr;
        git_commit_parent(&parent, commit, 0);
        git_commit_tree(&oldTree, parent);
        git_commit_free(parent);
    }

    git_diff *diff = nullptr;
    git_diff_tree_to_tree(&diff, repo, oldTree, newTree, nullptr);

    if (oldTree)
        git_tree_free(oldTree);
    git_tree_free(newTree);
    git_commit_free(commit);

    return printDiff(diff);
}
