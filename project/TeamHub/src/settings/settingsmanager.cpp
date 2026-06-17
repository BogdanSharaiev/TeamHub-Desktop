#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

SettingsManager &SettingsManager::instance()
{
    static SettingsManager s;
    return s;
}

static QString defaultPath()
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

void SettingsManager::load()
{
    filePath = defaultPath();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    jsonData = QJsonDocument::fromJson(f.readAll()).object();
}

void SettingsManager::save()
{
    if (filePath.isEmpty())
        filePath = defaultPath();
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(jsonData).toJson(QJsonDocument::Indented));
}

void SettingsManager::setNested(const QString &group, const QString &key, const QJsonValue &val)
{
    QJsonObject grp = jsonData.value(group).toObject();
    grp.insert(key, val);
    jsonData.insert(group, grp);
}

QString SettingsManager::fontFamily() const
{
    return jsonData.value("editor").toObject().value("fontFamily").toString("Courier New");
}
int SettingsManager::fontSize() const
{
    return jsonData.value("editor").toObject().value("fontSize").toInt(12);
}
int SettingsManager::tabWidth() const
{
    return jsonData.value("editor").toObject().value("tabWidth").toInt(4);
}
QString SettingsManager::theme() const
{
    return jsonData.value("editor").toObject().value("theme").toString("dark");
}
bool SettingsManager::autoSave() const
{
    return jsonData.value("editor").toObject().value("autoSave").toBool(false);
}

void SettingsManager::setFontFamily(const QString &v)
{
    setNested("editor", "fontFamily", v);
}
void SettingsManager::setFontSize(int v)
{
    setNested("editor", "fontSize", v);
}
void SettingsManager::setTabWidth(int v)
{
    setNested("editor", "tabWidth", v);
}
void SettingsManager::setTheme(const QString &v)
{
    setNested("editor", "theme", v);
}
void SettingsManager::setAutoSave(bool v)
{
    setNested("editor", "autoSave", v);
}

QString SettingsManager::audioInputDevice() const
{
    return jsonData.value("audio").toObject().value("inputDevice").toString();
}
QString SettingsManager::audioOutputDevice() const
{
    return jsonData.value("audio").toObject().value("outputDevice").toString();
}
void SettingsManager::setAudioInputDevice(const QString &v)
{
    setNested("audio", "inputDevice", v);
}
void SettingsManager::setAudioOutputDevice(const QString &v)
{
    setNested("audio", "outputDevice", v);
}

QString SettingsManager::lastProjectPath() const
{
    return jsonData.value("lastProjectPath").toString();
}

void SettingsManager::setLastProjectPath(const QString &v)
{
    jsonData.insert("lastProjectPath", v);
}
