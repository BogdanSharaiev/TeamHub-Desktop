#ifndef TERMINAL_H
#define TERMINAL_H

#include <QPlainTextEdit>
#include <QProcess>
#include <QStringList>
#include <QTabWidget>
#include <QWidget>

class TerminalEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit TerminalEdit(const QString &workingDir, QWidget *parent = nullptr);
    ~TerminalEdit() override;
    void killProcess();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess *process;
    int promptPos;
    QString currentDir;
    QStringList history;
    int historyIdx;

    void startShell();
    void appendOutput(const QString &text);
    void setPromptPos();
    QString currentInput() const;
    void clearCurrentInput();
    void submitCommand();
    void handleTab();
    void historyUp();
    void historyDown();
    void ensureCursorInInputZone();
};

class Terminal : public QWidget
{
    Q_OBJECT
public:
    explicit Terminal(QWidget *parent = nullptr);
    void setWorkingDirectory(const QString &path);
    void addTerminal();
    int terminalCount() const;
    void focusCurrent();
    void killAll();

private:
    QTabWidget *tabs;
    QString workingDir;
    int counter = 0;
};

#endif // TERMINAL_H
