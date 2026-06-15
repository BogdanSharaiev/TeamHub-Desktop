#ifndef AUTHDIALOG_H
#define AUTHDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "authmanager.h"

class AuthDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AuthDialog(AuthManager *auth, QWidget *parent = nullptr);

private:
    enum class Mode { Login, Register };

    AuthManager *auth;
    Mode mode = Mode::Login;

    QPushButton *tabLogin;
    QPushButton *tabRegister;

    QWidget *usernameRow;
    QLineEdit *usernameEdit;
    QLineEdit *emailEdit;
    QLineEdit *passwordEdit;
    QWidget *password2Row;
    QLineEdit *password2Edit;
    QLabel *errorLabel;
    QPushButton *submitBtn;

    void switchMode(Mode mode);
    void onSubmit();
    void setLoading(bool on);
    void showError(const QString &msg);
    void clearError();
};

#endif // AUTHDIALOG_H
