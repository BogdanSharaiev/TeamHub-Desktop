#ifndef TERMINAL_H
#define TERMINAL_H

#include <QPlainTextEdit>
#include <QProcess>
#include <QStringList>
#include <QWidget>

class TerminalEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit TerminalEdit(QWidget *parent = nullptr);
    ~TerminalEdit() override;

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess   *process;
    int         promptPos;
    QString     currentDir;
    QStringList history;
    int         historyIdx;

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

private:
    TerminalEdit *edit;
};

#endif // TERMINAL_H
