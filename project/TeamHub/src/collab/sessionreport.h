#ifndef SESSIONREPORT_H
#define SESSIONREPORT_H

#include <QList>
#include <QString>
#include <QStringList>

struct ParticipantStats
{
    int siteId = 0;
    bool isHost = false;
    int activeSec = 0;
    int totalInserts = 0;
    int totalDeletes = 0;
    QStringList filesTouched;
};

struct FileInfo
{
    QString name;
    QList<int> editorSiteIds;
};

struct AiInsights
{
    bool available = false;
    QString text;
};

struct SessionReportData
{
    QString roomName;
    QString startTime;
    QString endTime;
    int durationSec = 0;
    QList<ParticipantStats> participants;
    QList<FileInfo> files;
    AiInsights ai;
};

#endif // SESSIONREPORT_H
