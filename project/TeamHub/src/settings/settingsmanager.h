#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QJsonObject>
#include <QString>

class SettingsManager
{
public:
    static SettingsManager &instance();

    void load();
    void save();

    QString fontFamily() const;
    int fontSize() const;
    int tabWidth() const;
    QString theme() const;
    bool autoSave() const;

    void setFontFamily(const QString &v);
    void setFontSize(int v);
    void setTabWidth(int v);
    void setTheme(const QString &v);
    void setAutoSave(bool v);

    QString audioInputDevice() const;
    QString audioOutputDevice() const;
    void setAudioInputDevice(const QString &v);
    void setAudioOutputDevice(const QString &v);

    QString lastProjectPath() const;
    void setLastProjectPath(const QString &v);

private:
    SettingsManager() = default;
    QString filePath;
    QJsonObject jsonData;

    void setNested(const QString &group, const QString &key, const QJsonValue &val);
};

#endif // SETTINGSMANAGER_H
