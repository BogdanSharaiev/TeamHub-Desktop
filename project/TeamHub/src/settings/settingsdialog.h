#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFontComboBox>
#include <QSpinBox>

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void settingsApplied();

private:
    QFontComboBox *fontFamilyBox;
    QSpinBox *fontSizeBox;
    QSpinBox *tabWidthBox;
    QComboBox *themeBox;
    QCheckBox *autoSaveCheck;
    QComboBox *inputDeviceBox;
    QComboBox *outputDeviceBox;

    void loadValues();
    void applyValues();
    void populateAudioDevices();
};

#endif // SETTINGSDIALOG_H
