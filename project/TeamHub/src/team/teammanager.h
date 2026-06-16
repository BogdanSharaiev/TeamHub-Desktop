#ifndef TEAMMANAGER_H
#define TEAMMANAGER_H

#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class AuthManager;

class TeamManager : public QObject
{
    Q_OBJECT
public:
    struct UserRef
    {
        int id = 0;
        QString email;
        QString username;
        QString avatarUrl;
    };

    struct RoleInfo
    {
        QString id;
        QString name;
        bool isAdmin = false;
        bool canView = false;
        bool canCreateProjects = false;
        bool canEditTeam = false;
        bool canManageSettings = false;
        bool canDelete = false;
    };

    struct MemberInfo
    {
        QString id;
        UserRef user;
        bool hasRole = false;
        RoleInfo role;
    };

    struct TeamInfo
    {
        QString id;
        QString name;
        QString description;
        UserRef createdBy;
        int membersCount = 0;
    };

    struct TeamDetails
    {
        TeamInfo team;
        QList<MemberInfo> members;
        QList<RoleInfo> roles;
    };

    struct RoomInfo
    {
        QString id;
        QString roomKey;
        QString teamId;
        QString teamName;
        QString name;
        int maxParticipants = 0;
        UserRef createdBy;
    };

    explicit TeamManager(AuthManager *authManager, QObject *parent = nullptr);

    void fetchMyTeams();
    void createTeam(const QString &name, const QString &description);
    void fetchTeamDetails(const QString &teamId);
    void fetchTeamRooms(const QString &teamId);
    void createTeamRoom(const QString &teamId, const QString &name, int maxParticipants);
    void deleteTeamRoom(const QString &teamId, const QString &roomId);

signals:
    void myTeamsReceived(const QList<TeamInfo> &teams);
    void myTeamsFailed(const QString &error);

    void teamCreated(const TeamInfo &team);
    void teamCreateFailed(const QString &error);

    void teamDetailsReceived(const TeamDetails &details);
    void teamDetailsFailed(const QString &teamId, const QString &error);

    void teamRoomsReceived(const QString &teamId, const QList<RoomInfo> &rooms);
    void teamRoomsFailed(const QString &teamId, const QString &error);

    void roomCreated(const QString &teamId, const RoomInfo &room);
    void roomCreateFailed(const QString &error);

    void roomDeleted(const QString &teamId, const QString &roomId);
    void roomDeleteFailed(const QString &error);

private:
    AuthManager *authManager;
    QNetworkAccessManager *nam;

    QNetworkRequest makeRequest(const QString &path) const;
    static QString firstError(const QJsonObject &obj, const QString &fallback);
    UserRef parseUserRef(const QJsonObject &obj) const;
    static RoleInfo parseRole(const QJsonObject &obj);
    TeamInfo parseTeam(const QJsonObject &obj) const;
    MemberInfo parseMember(const QJsonObject &obj) const;
    RoomInfo parseRoom(const QJsonObject &obj) const;
};

#endif // TEAMMANAGER_H
