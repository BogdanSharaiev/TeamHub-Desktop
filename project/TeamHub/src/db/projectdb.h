#ifndef PROJECTDB_H
#define PROJECTDB_H

#include <QList>
#include <QString>

class ProjectDB
{
public:
    static ProjectDB &instance();

    struct FileState
    {
        QString path;
        int tabOrder;
        bool isActive;
    };

    struct ProjectInfo
    {
        int id;
        QString path;
        QString name;
        qint64 lastOpened;
    };

    bool open();

    int upsertProject(const QString &path, const QString &name);
    void removeProject(const QString &path);
    void saveOpenFiles(int projectId, const QList<FileState> &files);
    QList<FileState> loadOpenFiles(int projectId);
    QList<ProjectInfo> recentProjects(int limit = 10);

private:
    ProjectDB() = default;
    bool isOpen = false;

    void createSchema();
    int projectIdForPath(const QString &path);
};

#endif // PROJECTDB_H
