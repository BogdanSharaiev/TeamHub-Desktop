#ifndef SESSIONREPORTDIALOG_H
#define SESSIONREPORTDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "sessionreport.h"

class SessionReportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SessionReportDialog(const SessionReportData &report,
                                 bool endingSession = false,
                                 QWidget *parent = nullptr);

    void updateAiSection(const AiInsights &ai);

signals:
    void sessionEnded();

private:
    void buildContent(QVBoxLayout *layout);
    void saveTxt();
    void saveJson();
    QString formatDuration(int secs) const;
    QString reportToText() const;

    SessionReportData report;
    bool endingSession;

    QLabel *aiStatusLabel = nullptr;
    QWidget *aiBody = nullptr;
    QVBoxLayout *aiBodyLayout = nullptr;
    QTimer *spinnerTimer = nullptr;
    int spinnerStep = 0;

    static const QColor kColors[6];
    static QColor siteColor(int siteId);
};

#endif // SESSIONREPORTDIALOG_H
