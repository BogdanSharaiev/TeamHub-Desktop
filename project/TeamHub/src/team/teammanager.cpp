#include "teammanager.h"

#include "../auth/authmanager.h"
#include "../avatar/avatar.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

TeamManager::TeamManager(AuthManager *authManager, QObject *parent)
    : QObject(parent)
    , authManager(authManager)
    , nam(new QNetworkAccessManager(this))
{}

QNetworkRequest TeamManager::makeRequest(const QString &path) const
{
    QNetworkRequest req(QUrl(authManager->getBaseUrl() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (authManager->isLoggedIn())
        req.setRawHeader("Authorization", ("Token " + authManager->token()).toUtf8());
    return req;
}

QString TeamManager::firstError(const QJsonObject &obj, const QString &fallback)
{
    if (obj.contains("detail"))
        return obj["detail"].toString();

    const QJsonArray nonField = obj["non_field_errors"].toArray();
    if (!nonField.isEmpty())
        return nonField[0].toString();

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QJsonArray arr = it.value().toArray();
        if (!arr.isEmpty())
            return arr[0].toString();
    }

    return fallback;
}

TeamManager::UserRef TeamManager::parseUserRef(const QJsonObject &obj) const
{
    UserRef u;
    u.id = obj["id"].toInt();
    u.email = obj["email"].toString();
    u.username = obj["username"].toString();
    u.avatarUrl = Avatar::resolveUrl(authManager->getBaseUrl(), obj["avatar_url"].toString());
    return u;
}

TeamManager::RoleInfo TeamManager::parseRole(const QJsonObject &obj)
{
    RoleInfo r;
    r.id = obj["id"].toString();
    r.name = obj["name"].toString();
    r.isAdmin = obj["is_admin"].toBool();
    r.canView = obj["can_view"].toBool();
    r.canCreateProjects = obj["can_create_projects"].toBool();
    r.canEditTeam = obj["can_edit_team"].toBool();
    r.canManageSettings = obj["can_manage_settings"].toBool();
    r.canDelete = obj["can_delete"].toBool();
    return r;
}

TeamManager::TeamInfo TeamManager::parseTeam(const QJsonObject &obj) const
{
    TeamInfo t;
    t.id = obj["id"].toString();
    t.name = obj["name"].toString();
    t.description = obj["description"].toString();
    t.createdBy = parseUserRef(obj["created_by"].toObject());
    t.membersCount = obj["members_count"].toInt();
    return t;
}

TeamManager::MemberInfo TeamManager::parseMember(const QJsonObject &obj) const
{
    MemberInfo m;
    m.id = obj["id"].toString();
    m.user = parseUserRef(obj["user"].toObject());
    if (obj["role"].isObject() && !obj["role"].toObject().isEmpty()) {
        m.hasRole = true;
        m.role = parseRole(obj["role"].toObject());
    }
    return m;
}

TeamManager::RoomInfo TeamManager::parseRoom(const QJsonObject &obj) const
{
    RoomInfo r;
    r.id = obj["id"].toString();
    r.roomKey = obj["room_key"].toString();
    r.teamId = obj["team_id"].toString();
    r.teamName = obj["team_name"].toString();
    r.name = obj["name"].toString();
    r.maxParticipants = obj["max_participants"].toInt();
    r.createdBy = parseUserRef(obj["created_by"].toObject());
    return r;
}

void TeamManager::fetchMyTeams()
{
    auto *reply = nam->get(makeRequest("/api/auth/me/teams/"));

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            emit myTeamsFailed(firstError(root, reply->errorString()));
            return;
        }

        QList<TeamInfo> teams;
        const QJsonArray arr = QJsonDocument::fromJson(data).array();
        for (const QJsonValue &v : arr)
            teams << parseTeam(v.toObject());

        emit myTeamsReceived(teams);
    });
}

void TeamManager::createTeam(const QString &name, const QString &description)
{
    QJsonObject body;
    body["name"] = name;
    body["description"] = description;

    auto *reply = nam->post(makeRequest("/api/teams/"), QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            emit teamCreateFailed(firstError(root, reply->errorString()));
            return;
        }

        emit teamCreated(parseTeam(root));
    });
}

void TeamManager::fetchTeamDetails(const QString &teamId)
{
    auto *reply = nam->get(makeRequest("/api/teams/" + teamId + "/"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, teamId] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            emit teamDetailsFailed(teamId, firstError(root, reply->errorString()));
            return;
        }

        TeamDetails details;
        details.team = parseTeam(root);

        const QJsonArray members = root["members"].toArray();
        for (const QJsonValue &v : members)
            details.members << parseMember(v.toObject());

        const QJsonArray roles = root["roles"].toArray();
        for (const QJsonValue &v : roles)
            details.roles << parseRole(v.toObject());

        emit teamDetailsReceived(details);
    });
}

void TeamManager::fetchTeamRooms(const QString &teamId)
{
    auto *reply = nam->get(makeRequest("/api/teams/" + teamId + "/rooms/"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, teamId] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            emit teamRoomsFailed(teamId, firstError(root, reply->errorString()));
            return;
        }

        QList<RoomInfo> rooms;
        const QJsonArray arr = QJsonDocument::fromJson(data).array();
        for (const QJsonValue &v : arr)
            rooms << parseRoom(v.toObject());

        emit teamRoomsReceived(teamId, rooms);
    });
}

void TeamManager::createTeamRoom(const QString &teamId, const QString &name, int maxParticipants)
{
    QJsonObject body;
    body["name"] = name;
    body["max_participants"] = maxParticipants;

    auto *reply = nam->post(makeRequest("/api/teams/" + teamId + "/rooms/"), QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, teamId] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            emit roomCreateFailed(firstError(root, reply->errorString()));
            return;
        }

        emit roomCreated(teamId, parseRoom(root));
    });
}

void TeamManager::deleteTeamRoom(const QString &teamId, const QString &roomId)
{
    auto *reply = nam->deleteResource(makeRequest("/api/teams/" + teamId + "/rooms/" + roomId + "/"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, teamId, roomId] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            emit roomDeleteFailed(firstError(root, reply->errorString()));
            return;
        }

        emit roomDeleted(teamId, roomId);
    });
}
