#include "settingsdialog.h"
#include "settingsmanager.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QMediaDevices>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumWidth(440);

    QFile f(QCoreApplication::applicationDirPath() + "/styles/settingsdialog.qss");
    if (!f.open(QIODevice::ReadOnly))
        f.setFileName(QString(TEAMHUB_STYLES_DIR) + "settingsdialog.qss");
    if (f.isOpen() || f.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(f.readAll()));

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(0, 0, 0, 10);

    auto *tabs = new QTabWidget(this);

    // ── Editor tab ─────────────────────────────────────────────
    auto *editorPage = new QWidget;
    auto *editorForm = new QFormLayout(editorPage);
    editorForm->setSpacing(12);
    editorForm->setContentsMargins(16, 16, 16, 16);
    editorForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    fontFamilyBox = new QFontComboBox;
    fontFamilyBox->setFontFilters(QFontComboBox::MonospacedFonts);
    editorForm->addRow("Font family:", fontFamilyBox);

    fontSizeBox = new QSpinBox;
    fontSizeBox->setRange(8, 32);
    fontSizeBox->setSuffix(" pt");
    editorForm->addRow("Font size:", fontSizeBox);

    tabWidthBox = new QSpinBox;
    tabWidthBox->setRange(2, 8);
    tabWidthBox->setSuffix(" spaces");
    editorForm->addRow("Tab width:", tabWidthBox);

    themeBox = new QComboBox;
    themeBox->addItem("Dark",  "dark");
    themeBox->addItem("Light", "light");
    editorForm->addRow("Theme:", themeBox);

    autoSaveCheck = new QCheckBox("Auto-save before running");
    editorForm->addRow("", autoSaveCheck);

    tabs->addTab(editorPage, "Editor");

    // ── Audio tab ──────────────────────────────────────────────
    auto *audioPage = new QWidget;
    auto *audioForm = new QFormLayout(audioPage);
    audioForm->setSpacing(12);
    audioForm->setContentsMargins(16, 16, 16, 16);
    audioForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    inputDeviceBox  = new QComboBox;
    outputDeviceBox = new QComboBox;
    populateAudioDevices();
    audioForm->addRow("Microphone:", inputDeviceBox);
    audioForm->addRow("Speakers:",   outputDeviceBox);

    tabs->addTab(audioPage, "Audio");

    root->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Apply
                                         | QDialogButtonBox::Cancel,
                                         this);
    root->addWidget(buttons);
    root->setContentsMargins(0, 0, 8, 8);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        applyValues();
        accept();
    });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        applyValues();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadValues();
}

void SettingsDialog::populateAudioDevices()
{
    inputDeviceBox->addItem("System default", QString());
    for (const QAudioDevice &dev : QMediaDevices::audioInputs())
        inputDeviceBox->addItem(dev.description(), dev.id());

    outputDeviceBox->addItem("System default", QString());
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs())
        outputDeviceBox->addItem(dev.description(), dev.id());
}

void SettingsDialog::loadValues()
{
    auto &s = SettingsManager::instance();

    fontFamilyBox->setCurrentFont(QFont(s.fontFamily()));
    fontSizeBox->setValue(s.fontSize());
    tabWidthBox->setValue(s.tabWidth());

    const int themeIdx = themeBox->findData(s.theme());
    themeBox->setCurrentIndex(themeIdx >= 0 ? themeIdx : 0);
    autoSaveCheck->setChecked(s.autoSave());

    auto selectById = [](QComboBox *box, const QString &id) {
        for (int i = 0; i < box->count(); ++i) {
            if (box->itemData(i).toString() == id) {
                box->setCurrentIndex(i);
                return;
            }
        }
    };
    selectById(inputDeviceBox,  s.audioInputDevice());
    selectById(outputDeviceBox, s.audioOutputDevice());
}

void SettingsDialog::applyValues()
{
    auto &s = SettingsManager::instance();
    s.setFontFamily(fontFamilyBox->currentFont().family());
    s.setFontSize(fontSizeBox->value());
    s.setTabWidth(tabWidthBox->value());
    s.setTheme(themeBox->currentData().toString());
    s.setAutoSave(autoSaveCheck->isChecked());
    s.setAudioInputDevice(inputDeviceBox->currentData().toString());
    s.setAudioOutputDevice(outputDeviceBox->currentData().toString());
    s.save();
    emit settingsApplied();
}
