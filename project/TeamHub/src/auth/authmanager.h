#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QNetworkAccessManager>
#include <QObject>

class AuthManager : public QObject
{
    Q_OBJECT
public:
    struct UserInfo
    {
        int id = 0;
        QString email;
        QString username;
        QString avatarUrl;
    };

    explicit AuthManager(QObject *parent = nullptr);

    bool isLoggedIn() const { return !authToken.isEmpty(); }
    QString token() const { return authToken; }
    UserInfo currentUser() const { return userData; }

    void setBaseUrl(const QString &url) { baseUrl = url; }

    void login(const QString &email, const QString &password);
    void registerUser(const QString &email,
                      const QString &username,
                      const QString &password,
                      const QString &password2);
    void logout();
    void loadSavedSession();

signals:
    void loginSuccess(const UserInfo &user);
    void loginFailed(const QString &error);
    void registerSuccess(const UserInfo &user);
    void registerFailed(const QString &error);
    void logoutFinished();
    void sessionRestored(const UserInfo &user);

private:
    QNetworkAccessManager *nam;
    QString baseUrl = "http://localhost:8000";
    QString authToken;
    UserInfo userData;

    QNetworkRequest makeRequest(const QString &path) const;
    static UserInfo parseUser(const QJsonObject &obj);
    void saveSession();
    void clearSession();
};

#endif // AUTHMANAGER_H
