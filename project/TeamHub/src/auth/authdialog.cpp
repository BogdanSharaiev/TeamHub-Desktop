#include "authdialog.h"

#include <QCoreApplication>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

static QWidget *makeFieldRow(const QString &labelText,
                             QLineEdit *&fieldOut,
                             const QString &placeholder,
                             bool password = false)
{
    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *vbox = new QVBoxLayout(row);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(5);

    auto *lbl = new QLabel(labelText);
    lbl->setStyleSheet("color: #cccccc; font-size: 12px;");

    fieldOut = new QLineEdit;
    fieldOut->setPlaceholderText(placeholder);
    if (password)
        fieldOut->setEchoMode(QLineEdit::Password);

    vbox->addWidget(lbl);
    vbox->addWidget(fieldOut);
    return row;
}

AuthDialog::AuthDialog(AuthManager *auth, QWidget *parent)
    : QDialog(parent)
    , auth(auth)
{
    setWindowTitle("TeamHub");
    setFixedWidth(380);
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    {
        QFile f(QCoreApplication::applicationDirPath() + "/styles/authdialog.qss");
        if (!f.open(QIODevice::ReadOnly))
            f.setFileName(QString(TEAMHUB_STYLES_DIR) + "authdialog.qss");
        if (f.isOpen() || f.open(QIODevice::ReadOnly))
            setStyleSheet(QString::fromUtf8(f.readAll()));
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(36, 32, 36, 32);
    root->setSpacing(0);
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto *titleLbl = new QLabel("TeamHub");
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setStyleSheet(
        "font-size: 24px; font-weight: bold; color: #cccccc; background: transparent;");
    root->addWidget(titleLbl);
    root->addSpacing(24);

    auto *tabRow = new QHBoxLayout;
    tabRow->setSpacing(0);
    tabRow->setContentsMargins(0, 0, 0, 0);

    tabLogin = new QPushButton("Sign In");
    tabLogin->setObjectName("tabBtn");
    tabLogin->setCheckable(true);
    tabLogin->setChecked(true);

    tabRegister = new QPushButton("Register");
    tabRegister->setObjectName("tabBtn");
    tabRegister->setCheckable(true);

    tabRow->addWidget(tabLogin);
    tabRow->addWidget(tabRegister);
    root->addLayout(tabRow);

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background-color: #3c3c3c; border: none;");
    root->addWidget(sep);
    root->addSpacing(20);

    usernameRow = makeFieldRow("Username", usernameEdit, "johndoe");
    root->addWidget(usernameRow);
    root->addSpacing(14);

    auto *emailRow = makeFieldRow("Email", emailEdit, "user@example.com");
    root->addWidget(emailRow);
    root->addSpacing(14);

    auto *pwRow = makeFieldRow("Password", passwordEdit, "", true);
    root->addWidget(pwRow);
    root->addSpacing(14);

    password2Row = makeFieldRow("Confirm Password", password2Edit, "", true);
    root->addWidget(password2Row);

    errorLabel = new QLabel;
    errorLabel->setStyleSheet("color: #f48771; font-size: 12px; background: transparent;");
    errorLabel->setWordWrap(true);
    errorLabel->hide();
    root->addSpacing(10);
    root->addWidget(errorLabel);

    root->addSpacing(20);

    submitBtn = new QPushButton("Sign In");
    submitBtn->setObjectName("submitBtn");
    root->addWidget(submitBtn);

    connect(tabLogin, &QPushButton::clicked, this, [this] { switchMode(Mode::Login); });
    connect(tabRegister, &QPushButton::clicked, this, [this] { switchMode(Mode::Register); });
    connect(submitBtn, &QPushButton::clicked, this, &AuthDialog::onSubmit);

    connect(auth, &AuthManager::loginSuccess, this, [this](const AuthManager::UserInfo &) {
        accept();
    });
    connect(auth, &AuthManager::loginFailed, this, [this](const QString &err) {
        setLoading(false);
        showError(err);
    });
    connect(auth, &AuthManager::registerSuccess, this, [this](const AuthManager::UserInfo &) {
        accept();
    });
    connect(auth, &AuthManager::registerFailed, this, [this](const QString &err) {
        setLoading(false);
        showError(err);
    });

    switchMode(Mode::Login);
}

void AuthDialog::switchMode(Mode mode)
{
    this->mode = mode;
    const bool reg = (mode == Mode::Register);

    tabLogin->setChecked(!reg);
    tabRegister->setChecked(reg);

    usernameRow->setVisible(reg);
    password2Row->setVisible(reg);

    submitBtn->setText(reg ? "Create Account" : "Sign In");
    setWindowTitle(reg ? "TeamHub — Register" : "TeamHub — Sign In");

    clearError();
}

void AuthDialog::onSubmit()
{
    clearError();

    const QString email = emailEdit->text().trimmed();
    const QString password = passwordEdit->text();

    if (email.isEmpty() || password.isEmpty()) {
        showError("Please fill in all required fields.");
        return;
    }

    if (mode == Mode::Register) {
        const QString username = usernameEdit->text().trimmed();
        const QString password2 = password2Edit->text();
        if (username.isEmpty()) {
            showError("Username is required.");
            return;
        }
        if (password != password2) {
            showError("Passwords do not match.");
            return;
        }
        setLoading(true);
        auth->registerUser(email, username, password, password2);
    } else {
        setLoading(true);
        auth->login(email, password);
    }
}

void AuthDialog::setLoading(bool on)
{
    submitBtn->setEnabled(!on);
    submitBtn->setText(on ? "Please wait…"
                          : (mode == Mode::Register ? "Create Account" : "Sign In"));
    emailEdit->setEnabled(!on);
    passwordEdit->setEnabled(!on);
    usernameEdit->setEnabled(!on);
    password2Edit->setEnabled(!on);
    tabLogin->setEnabled(!on);
    tabRegister->setEnabled(!on);
}

void AuthDialog::showError(const QString &msg)
{
    errorLabel->setText(msg);
    errorLabel->show();
}

void AuthDialog::clearError()
{
    errorLabel->hide();
    errorLabel->clear();
}
