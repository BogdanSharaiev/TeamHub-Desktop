#include "projectdb.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

static constexpr char DB_CONN[] = "teamhub_projects";

ProjectDB &ProjectDB::instance()
{
    static ProjectDB db;
    return db;
}

bool ProjectDB::open()
{
    if (isOpen)
        return true;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", DB_CONN);
    db.setDatabaseName(QCoreApplication::applicationDirPath() + "/projects.db");

    if (!db.open()) {
        qWarning() << "[ProjectDB] Failed to open:" << db.lastError().text();
        return false;
    }

    createSchema();
    isOpen = true;
    return true;
}

void ProjectDB::createSchema()
{
    QSqlQuery q(QSqlDatabase::database(DB_CONN));

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS projects (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            path        TEXT    UNIQUE NOT NULL,
            name        TEXT    NOT NULL,
            last_opened INTEGER NOT NULL
        )
    )"))
        qWarning() << "[ProjectDB] create projects:" << q.lastError().text();

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS open_files (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
            file_path   TEXT    NOT NULL,
            tab_order   INTEGER DEFAULT 0,
            is_active   INTEGER DEFAULT 0
        )
    )"))
        qWarning() << "[ProjectDB] create open_files:" << q.lastError().text();

    q.exec("PRAGMA foreign_keys = ON");
}

int ProjectDB::projectIdForPath(const QString &path)
{
    QSqlQuery q(QSqlDatabase::database(DB_CONN));
    q.prepare("SELECT id FROM projects WHERE path = :path");
    q.bindValue(":path", path);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

int ProjectDB::upsertProject(const QString &path, const QString &name)
{
    if (!isOpen)
        return -1;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int existing = projectIdForPath(path);
    QSqlQuery q(QSqlDatabase::database(DB_CONN));

    if (existing >= 0) {
        q.prepare("UPDATE projects SET name = :name, last_opened = :ts WHERE id = :id");
        q.bindValue(":name", name);
        q.bindValue(":ts",   now);
        q.bindValue(":id",   existing);
        if (!q.exec())
            qWarning() << "[ProjectDB] UPDATE failed:" << q.lastError().text();
        return existing;
    }

    q.prepare("INSERT INTO projects (path, name, last_opened) VALUES (:path, :name, :ts)");
    q.bindValue(":path", path);
    q.bindValue(":name", name);
    q.bindValue(":ts",   now);
    if (!q.exec()) {
        qWarning() << "[ProjectDB] INSERT failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

void ProjectDB::removeProject(const QString &path)
{
    if (!isOpen)
        return;

    QSqlQuery q(QSqlDatabase::database(DB_CONN));
    q.prepare("DELETE FROM projects WHERE path = :path");
    q.bindValue(":path", path);
    if (!q.exec())
        qWarning() << "[ProjectDB] DELETE project failed:" << q.lastError().text();
}

void ProjectDB::saveOpenFiles(int projectId, const QList<FileState> &files)
{
    if (!isOpen || projectId < 0)
        return;

    QSqlQuery q(QSqlDatabase::database(DB_CONN));
    q.prepare("DELETE FROM open_files WHERE project_id = :pid");
    q.bindValue(":pid", projectId);
    q.exec();

    q.prepare(R"(
        INSERT INTO open_files (project_id, file_path, tab_order, is_active)
        VALUES (:pid, :path, :ord, :active)
    )");
    for (const FileState &f : files) {
        q.bindValue(":pid",    projectId);
        q.bindValue(":path",   f.path);
        q.bindValue(":ord",    f.tabOrder);
        q.bindValue(":active", f.isActive ? 1 : 0);
        if (!q.exec())
            qWarning() << "[ProjectDB] insert open_file:" << q.lastError().text();
    }
}

QList<ProjectDB::FileState> ProjectDB::loadOpenFiles(int projectId)
{
    QList<FileState> result;
    if (!isOpen || projectId < 0)
        return result;

    QSqlQuery q(QSqlDatabase::database(DB_CONN));
    q.prepare(R"(
        SELECT file_path, tab_order, is_active
        FROM   open_files
        WHERE  project_id = :pid
        ORDER  BY tab_order
    )");
    q.bindValue(":pid", projectId);
    if (!q.exec())
        return result;

    while (q.next()) {
        result.append({
            q.value(0).toString(),
            q.value(1).toInt(),
            q.value(2).toInt() != 0
        });
    }
    return result;
}

QList<ProjectDB::ProjectInfo> ProjectDB::recentProjects(int limit)
{
    QList<ProjectInfo> result;
    if (!isOpen)
        return result;

    QSqlQuery q(QSqlDatabase::database(DB_CONN));
    q.prepare(R"(
        SELECT id, path, name, last_opened
        FROM   projects
        ORDER  BY last_opened DESC
        LIMIT  :lim
    )");
    q.bindValue(":lim", limit);
    if (!q.exec())
        return result;

    while (q.next()) {
        result.append({
            q.value(0).toInt(),
            q.value(1).toString(),
            q.value(2).toString(),
            q.value(3).toLongLong()
        });
    }
    return result;
}
