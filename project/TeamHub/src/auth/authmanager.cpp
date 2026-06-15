#include "authmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
    , nam(new QNetworkAccessManager(this))
{}

void AuthManager::loadSavedSession()
{
    QSettings s;
    authToken = s.value("auth/token").toString();
    userData.id = s.value("auth/userId").toInt();
    userData.email = s.value("auth/email").toString();
    userData.username = s.value("auth/username").toString();
    userData.avatarUrl = s.value("auth/avatarUrl").toString();

    if (!authToken.isEmpty())
        emit sessionRestored(userData);
}

QNetworkRequest AuthManager::makeRequest(const QString &path) const
{
    QNetworkRequest req(QUrl(baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!authToken.isEmpty())
        req.setRawHeader("Authorization", ("Token " + authToken).toUtf8());
    return req;
}

void AuthManager::login(const QString &email, const QString &password)
{
    QJsonObject body;
    body["email"] = email;
    body["password"] = password;

    auto *reply = nam->post(makeRequest("/api/auth/login/"), QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            const QJsonArray errs = root["non_field_errors"].toArray();
            emit loginFailed(errs.isEmpty() ? reply->errorString() : errs[0].toString());
            return;
        }

        authToken = root["token"].toString();
        userData = parseUser(root["user"].toObject());
        saveSession();
        emit loginSuccess(userData);
    });
}

void AuthManager::registerUser(const QString &email,
                               const QString &username,
                               const QString &password,
                               const QString &password2)
{
    QJsonObject body;
    body["email"] = email;
    body["username"] = username;
    body["password"] = password;
    body["password2"] = password2;
    body["language"] = "uk";

    auto *reply = nam->post(makeRequest("/api/auth/register/"), QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            QStringList msgs;
            for (auto it = root.begin(); it != root.end(); ++it) {
                const QJsonArray arr = it.value().toArray();
                if (!arr.isEmpty())
                    msgs << arr[0].toString();
            }
            emit registerFailed(msgs.isEmpty() ? reply->errorString() : msgs.join("\n"));
            return;
        }

        authToken = root["token"].toString();
        userData = parseUser(root["user"].toObject());
        saveSession();
        emit registerSuccess(userData);
    });
}

void AuthManager::logout()
{
    if (!isLoggedIn()) {
        clearSession();
        emit logoutFinished();
        return;
    }

    auto *reply = nam->post(makeRequest("/api/auth/logout/"), QByteArray{});
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        clearSession();
        emit logoutFinished();
    });
}

AuthManager::UserInfo AuthManager::parseUser(const QJsonObject &obj)
{
    UserInfo u;
    u.id = obj["id"].toInt();
    u.email = obj["email"].toString();
    u.username = obj["username"].toString();
    u.avatarUrl = obj["avatar_url"].toString();
    return u;
}

void AuthManager::saveSession()
{
    QSettings s;
    s.setValue("auth/token", authToken);
    s.setValue("auth/userId", userData.id);
    s.setValue("auth/email", userData.email);
    s.setValue("auth/username", userData.username);
    s.setValue("auth/avatarUrl", userData.avatarUrl);
}

void AuthManager::clearSession()
{
    authToken = {};
    userData = {};
    QSettings s;
    s.remove("auth/token");
    s.remove("auth/userId");
    s.remove("auth/email");
    s.remove("auth/username");
    s.remove("auth/avatarUrl");
}
