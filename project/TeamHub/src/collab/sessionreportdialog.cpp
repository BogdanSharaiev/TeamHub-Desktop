#include "sessionreportdialog.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollArea>

const QColor SessionReportDialog::kColors[6] = {
    QColor("#4ec9b0"),
    QColor("#f44747"),
    QColor("#d7ba7d"),
    QColor("#7fb3d3"),
    QColor("#a9dc76"),
    QColor("#ab9df2"),
};

QColor SessionReportDialog::siteColor(int siteId)
{
    return kColors[qAbs(siteId) % 6];
}

QString SessionReportDialog::formatDuration(int secs) const
{
    const int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    if (h > 0)
        return QString("%1h %2m").arg(h).arg(m);
    if (m > 0)
        return QString("%1m %2s").arg(m).arg(s);
    return QString("%1s").arg(s);
}

// ── constructor ───────────────────────────────────────────────────────────────

SessionReportDialog::SessionReportDialog(const SessionReportData &report,
                                         bool endingSession,
                                         QWidget *parent)
    : QDialog(parent)
    , report(report)
    , endingSession(endingSession)
{
    setWindowTitle(QString("Session Report — %1").arg(report.roomName));
    setMinimumSize(560, 500);
    resize(640, 620);

    setStyleSheet(
        "QDialog                { background:#1e1e1e; color:#d4d4d4; }"
        "QWidget                { background:#1e1e1e; color:#d4d4d4; }"
        "QLabel                 { background:transparent; color:#d4d4d4; }"
        "QGroupBox              { border:1px solid #3c3c3c; border-radius:4px;"
        "                         margin-top:10px; font-weight:bold;"
        "                         color:#9cdcfe; padding:6px 8px 8px 8px; }"
        "QGroupBox::title       { subcontrol-origin:margin; left:8px; padding:0 4px; }"
        "QPushButton            { background:#3a3d41; color:#d4d4d4;"
        "                         border:1px solid #555; border-radius:3px; padding:5px 14px; }"
        "QPushButton:hover      { background:#4a4d51; }"
        "QPushButton#primary    { background:#007acc; border-color:#007acc; color:#fff; }"
        "QPushButton#primary:hover { background:#1a8fe0; }"
        "QScrollArea            { border:none; }"
        "QScrollBar:vertical    { background:#1e1e1e; width:8px; }"
        "QScrollBar::handle:vertical { background:#3c3c3c; border-radius:4px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QFrame#sep             { background:#3c3c3c; max-height:1px; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    // header
    auto *hdr = new QLabel(
        QString("<b style='color:#9cdcfe;font-size:13px;'>TEAMHUB SESSION REPORT</b>"
                "&nbsp;&nbsp;"
                "<span style='color:#808080;'>%1"
                "&nbsp;·&nbsp;%2 – %3"
                "&nbsp;·&nbsp;<b style='color:#d4d4d4;'>%4</b></span>")
            .arg(report.roomName)
            .arg(report.startTime.mid(11, 5))
            .arg(report.endTime.mid(11, 5))
            .arg(formatDuration(report.durationSec)));
    hdr->setTextFormat(Qt::RichText);
    root->addWidget(hdr);

    auto *sep = new QFrame;
    sep->setObjectName("sep");
    sep->setFrameShape(QFrame::HLine);
    root->addWidget(sep);

    // scroll area with all content
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *contentWidget = new QWidget;
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 6, 0);
    contentLayout->setSpacing(10);

    buildContent(contentLayout);
    contentLayout->addStretch();

    scroll->setWidget(contentWidget);
    root->addWidget(scroll, 1);

    // buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);

    auto *btnTxt = new QPushButton("Save TXT");
    auto *btnJson = new QPushButton("Save JSON");
    connect(btnTxt, &QPushButton::clicked, this, &SessionReportDialog::saveTxt);
    connect(btnJson, &QPushButton::clicked, this, &SessionReportDialog::saveJson);
    btnRow->addWidget(btnTxt);
    btnRow->addWidget(btnJson);
    btnRow->addStretch();

    if (endingSession) {
        auto *btnEnd = new QPushButton("Close && End Collab");
        btnEnd->setObjectName("primary");
        connect(btnEnd, &QPushButton::clicked, this, [this]() {
            emit sessionEnded();
            accept();
        });
        btnRow->addWidget(btnEnd);
    } else {
        auto *btnClose = new QPushButton("Close");
        connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
        btnRow->addWidget(btnClose);
    }
    root->addLayout(btnRow);
}

// ── content ───────────────────────────────────────────────────────────────────

void SessionReportDialog::buildContent(QVBoxLayout *layout)
{
    // ── participants ──────────────────────────────────────────────────────────
    auto *partGroup = new QGroupBox("PARTICIPANTS");
    auto *partLayout = new QVBoxLayout(partGroup);
    partLayout->setSpacing(6);

    for (const ParticipantStats &p : report.participants) {
        auto *card = new QWidget;
        card->setStyleSheet(QString("QWidget { background:#252526; border:1px solid %1;"
                                    " border-radius:4px; } QLabel { background:transparent; }")
                                .arg(siteColor(p.siteId).name()));
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(10, 7, 10, 7);
        cl->setSpacing(3);

        // title row
        auto *row1 = new QHBoxLayout;
        const QString badge = p.isHost ? " <span style='color:#f9c74f;'>[Host]</span>" : "";
        auto *title = new QLabel(QString("<b style='color:%1;'>Site %2</b>%3")
                                     .arg(siteColor(p.siteId).name())
                                     .arg(p.siteId)
                                     .arg(badge));
        title->setTextFormat(Qt::RichText);
        row1->addWidget(title);
        row1->addStretch();
        auto *timeL = new QLabel(QString("<span style='color:#808080;'>active %1</span>")
                                     .arg(formatDuration(p.activeSec)));
        timeL->setTextFormat(Qt::RichText);
        row1->addWidget(timeL);
        cl->addLayout(row1);

        // stats row
        auto *row2 = new QHBoxLayout;
        auto *insL = new QLabel(
            QString("<span style='color:#4ec9b0;font-size:13px;font-weight:bold;'>%1</span>"
                    "<span style='color:#808080;'> inserts</span>")
                .arg(p.totalInserts));
        insL->setTextFormat(Qt::RichText);
        auto *delL = new QLabel(
            QString("<span style='color:#f44747;font-size:13px;font-weight:bold;'>%1</span>"
                    "<span style='color:#808080;'> deletes</span>")
                .arg(p.totalDeletes));
        delL->setTextFormat(Qt::RichText);
        row2->addWidget(insL);
        row2->addSpacing(14);
        row2->addWidget(delL);
        row2->addStretch();
        cl->addLayout(row2);

        // files touched
        if (!p.filesTouched.isEmpty()) {
            auto *filesL = new QLabel(
                QString("<span style='color:#808080;font-size:10px;'>%1</span>")
                    .arg(p.filesTouched.join("  ·  ")));
            filesL->setTextFormat(Qt::RichText);
            filesL->setWordWrap(true);
            cl->addWidget(filesL);
        }

        partLayout->addWidget(card);
    }
    layout->addWidget(partGroup);

    // ── files ─────────────────────────────────────────────────────────────────
    if (!report.files.isEmpty()) {
        auto *filesGroup = new QGroupBox("FILES EDITED");
        auto *fl = new QVBoxLayout(filesGroup);
        fl->setSpacing(4);

        for (const FileInfo &fi : report.files) {
            QStringList editors;
            for (int sid : fi.editorSiteIds)
                editors.append(QString("<span style='color:%1;'>Site %2</span>")
                                   .arg(siteColor(sid).name())
                                   .arg(sid));
            auto *lbl = new QLabel(QString("<span style='color:#9cdcfe;'>%1</span>"
                                           "<span style='color:#555;'> — </span>%2")
                                       .arg(fi.name)
                                       .arg(editors.join("<span style='color:#555;'>, </span>")));
            lbl->setTextFormat(Qt::RichText);
            fl->addWidget(lbl);
        }
        layout->addWidget(filesGroup);
    }

    // ── AI insights ───────────────────────────────────────────────────────────
    auto *aiGroup = new QGroupBox("AI INSIGHTS");
    auto *aiOuter = new QVBoxLayout(aiGroup);
    aiOuter->setSpacing(6);

    aiStatusLabel = new QLabel("● Generating...");
    aiStatusLabel->setStyleSheet("color:#808080; font-size:11px; background:transparent;");
    aiOuter->addWidget(aiStatusLabel);

    aiBody = new QWidget;
    aiBody->setStyleSheet("background:transparent;");
    aiBodyLayout = new QVBoxLayout(aiBody);
    aiBodyLayout->setContentsMargins(0, 0, 0, 0);
    aiBodyLayout->setSpacing(8);
    aiBody->hide();
    aiOuter->addWidget(aiBody);

    layout->addWidget(aiGroup);

    // spinner
    spinnerTimer = new QTimer(this);
    connect(spinnerTimer, &QTimer::timeout, this, [this]() {
        const char *frames[] = {"●", "◕", "◑", "◔"};
        spinnerStep = (spinnerStep + 1) % 4;
        aiStatusLabel->setText(QString("%1 Generating...").arg(frames[spinnerStep]));
    });
    spinnerTimer->start(400);

    QTimer::singleShot(25000, this, [this]() {
        if (!report.ai.available) {
            spinnerTimer->stop();
            aiStatusLabel->setText("unavailable — set GEMINI_API_KEY in server/.env");
        }
    });
}

// ── AI update ─────────────────────────────────────────────────────────────────

void SessionReportDialog::updateAiSection(const AiInsights &ai)
{
    report.ai = ai;
    spinnerTimer->stop();
    aiStatusLabel->setText("✓ Ready");
    aiStatusLabel->setStyleSheet("color:#4ec9b0; font-size:11px; background:transparent;");

    // summary
    if (!ai.summary.isEmpty()) {
        auto *lbl = new QLabel(ai.summary);
        lbl->setWordWrap(true);
        lbl->setTextFormat(Qt::PlainText);
        lbl->setStyleSheet("color:#d4d4d4; font-size:12px; background:transparent;");
        aiBodyLayout->addWidget(lbl);
    }

    // per-participant descriptions
    for (auto it = ai.participantWork.constBegin(); it != ai.participantWork.constEnd(); ++it) {
        const int sid = it.key();
        auto *row = new QWidget;
        row->setStyleSheet("background:transparent;");
        auto *rl = new QVBoxLayout(row);
        rl->setContentsMargins(0, 4, 0, 0);
        rl->setSpacing(2);

        auto *nameL = new QLabel(
            QString("<b style='color:%1;'>Site %2</b>").arg(siteColor(sid).name()).arg(sid));
        nameL->setTextFormat(Qt::RichText);
        nameL->setStyleSheet("background:transparent;");
        rl->addWidget(nameL);

        auto *descL = new QLabel(it.value());
        descL->setWordWrap(true);
        descL->setTextFormat(Qt::PlainText);
        descL->setStyleSheet(
            "color:#a0a0a0; font-size:11px; padding-left:10px; background:transparent;");
        rl->addWidget(descL);

        aiBodyLayout->addWidget(row);
    }

    aiBody->show();
}

// ── save ──────────────────────────────────────────────────────────────────────

QString SessionReportDialog::reportToText() const
{
    QString out;
    out += "TEAMHUB SESSION REPORT\n======================\n";
    out += QString("Room     : %1\n").arg(report.roomName);
    out += QString("Start    : %1\n").arg(report.startTime);
    out += QString("End      : %1\n").arg(report.endTime);
    out += QString("Duration : %1\n\n").arg(formatDuration(report.durationSec));

    out += "PARTICIPANTS\n------------\n";
    for (const ParticipantStats &p : report.participants) {
        out += QString("Site %1%2\n").arg(p.siteId).arg(p.isHost ? " (Host)" : "");
        out += QString("  Inserts: %1  Deletes: %2  Active: %3\n")
                   .arg(p.totalInserts)
                   .arg(p.totalDeletes)
                   .arg(formatDuration(p.activeSec));
        if (!p.filesTouched.isEmpty())
            out += QString("  Files: %1\n").arg(p.filesTouched.join(", "));
        out += "\n";
    }

    if (!report.files.isEmpty()) {
        out += "FILES EDITED\n------------\n";
        for (const FileInfo &fi : report.files) {
            QStringList eds;
            for (int sid : fi.editorSiteIds)
                eds.append(QString("Site %1").arg(sid));
            out += QString("%1 — %2\n").arg(fi.name).arg(eds.join(", "));
        }
        out += "\n";
    }

    if (report.ai.available) {
        out += "AI INSIGHTS\n-----------\n";
        out += report.ai.summary + "\n\n";
        for (auto it = report.ai.participantWork.constBegin();
             it != report.ai.participantWork.constEnd();
             ++it)
            out += QString("Site %1: %2\n").arg(it.key()).arg(it.value());
    }
    return out;
}

void SessionReportDialog::saveTxt()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save Report",
                                                      QString("report_%1.txt").arg(report.roomName),
                                                      "Text (*.txt)");
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(reportToText().toUtf8());
}

void SessionReportDialog::saveJson()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save Report",
                                                      QString("report_%1.json").arg(report.roomName),
                                                      "JSON (*.json)");
    if (path.isEmpty())
        return;

    QJsonObject root;
    root["room"] = report.roomName;
    root["start_time"] = report.startTime;
    root["end_time"] = report.endTime;
    root["duration_sec"] = report.durationSec;

    QJsonArray parts;
    for (const ParticipantStats &p : report.participants) {
        QJsonObject po;
        po["site_id"] = p.siteId;
        po["is_host"] = p.isHost;
        po["active_sec"] = p.activeSec;
        po["total_inserts"] = p.totalInserts;
        po["total_deletes"] = p.totalDeletes;
        QJsonArray ft;
        for (const QString &f : p.filesTouched)
            ft.append(f);
        po["files_touched"] = ft;
        parts.append(po);
    }
    root["participants"] = parts;

    QJsonArray files;
    for (const FileInfo &fi : report.files) {
        QJsonObject fo;
        fo["name"] = fi.name;
        QJsonArray eds;
        for (int sid : fi.editorSiteIds)
            eds.append(sid);
        fo["editors"] = eds;
        files.append(fo);
    }
    root["files"] = files;

    if (report.ai.available) {
        QJsonObject ai;
        ai["summary"] = report.ai.summary;
        QJsonObject pw;
        for (auto it = report.ai.participantWork.constBegin();
             it != report.ai.participantWork.constEnd();
             ++it)
            pw[QString::number(it.key())] = it.value();
        ai["participant_work"] = pw;
        root["ai"] = ai;
    }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}
