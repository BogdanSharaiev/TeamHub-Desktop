#ifndef GITLOGDIALOG_H
#define GITLOGDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QTextEdit>

#include "gitmanager.h"

class GitLogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitLogDialog(GitManager *git, QWidget *parent = nullptr);

private:
    GitManager   *git;
    QTableWidget *logTable;
    QTextEdit    *diffView;

    void loadLog();
    static void applyDiff(QTextEdit *view, const QString &raw);
};

#endif // GITLOGDIALOG_H
