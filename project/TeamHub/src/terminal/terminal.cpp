#include "terminal.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

TerminalEdit::TerminalEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , process(new QProcess(this))
    , promptPos(0)
    , historyIdx(-1)
{
    QFont font("Consolas", 10);
    font.setFixedPitch(true);
    setFont(font);
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);

    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::readyReadStandardOutput,
            this, &TerminalEdit::onReadyRead);
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalEdit::onProcessFinished);

    startShell();
}

TerminalEdit::~TerminalEdit()
{
    if (process->state() != QProcess::NotRunning) {
        process->write("exit\r\n");
        process->waitForFinished(800);
        process->kill();
    }
}

void TerminalEdit::startShell()
{
    currentDir = QDir::toNativeSeparators(QDir::homePath());
    process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process->start("cmd.exe", {"/Q", "/K", "PROMPT $P$G"});
}

void TerminalEdit::setWorkingDirectory(const QString &path){
    currentDir = QDir::toNativeSeparators(path);
    if(process->state() == QProcess::Running){
        process->write(("cd /d " + currentDir + "\r\n").toLocal8Bit());
    }
}

void TerminalEdit::appendOutput(const QString &text)
{
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
    insertPlainText(text);
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalEdit::setPromptPos()
{
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    promptPos = c.position();
}

void TerminalEdit::onReadyRead()
{
    QByteArray raw = process->readAllStandardOutput();
    QString text = QString::fromLocal8Bit(raw);

    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    static const QRegularExpression promptRe(
        R"(([A-Za-z]:[^>\n]*)>)",
        QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = promptRe.globalMatch(text);
    QRegularExpressionMatch last;
    while (it.hasNext()) last = it.next();
    if (last.hasMatch())
        currentDir = last.captured(1);

    appendOutput(text);
    setPromptPos();
}

void TerminalEdit::onProcessFinished(int, QProcess::ExitStatus)
{
    appendOutput("\n[shell exited — restarting]\n");
    startShell();
}

void TerminalEdit::ensureCursorInInputZone()
{
    if (textCursor().position() < promptPos) {
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::End);
        setTextCursor(c);
    }
}

QString TerminalEdit::currentInput() const
{
    QTextCursor c = textCursor();
    c.setPosition(promptPos);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return c.selectedText().replace(QChar(0x2029), '\n');
}

void TerminalEdit::clearCurrentInput()
{
    QTextCursor c = textCursor();
    c.setPosition(promptPos);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
}

void TerminalEdit::submitCommand()
{
    QString cmd = currentInput();

    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
    insertPlainText("\n");

    if (!cmd.trimmed().isEmpty()) {
        history.prepend(cmd);
        if (history.size() > 500) history.removeLast();
    }
    historyIdx = -1;

    process->write((cmd + "\r\n").toLocal8Bit());
}

void TerminalEdit::handleTab()
{
    QString input  = currentInput();
    QString prefix = input.section(' ', -1);
    QString base   = input.left(input.length() - prefix.length());

    QString searchDir = currentDir;
    QString filePrefix = prefix;
    int lastSlash = prefix.lastIndexOf(QRegularExpression(R"([/\\])"));
    if (lastSlash >= 0) {
        QString subDir = prefix.left(lastSlash + 1);
        if (QFileInfo(subDir).isAbsolute())
            searchDir = subDir;
        else
            searchDir = currentDir + "\\" + subDir;
        filePrefix = prefix.mid(lastSlash + 1);
    }

    QDir dir(searchDir);
    QStringList matches = dir.entryList(
        QStringList() << (filePrefix + "*"),
        QDir::AllEntries | QDir::NoDotAndDotDot);

    if (matches.isEmpty()) return;

    if (matches.size() == 1) {
        QString completed = prefix.left(prefix.length() - filePrefix.length()) + matches.first();
        if (QFileInfo(searchDir + "/" + matches.first()).isDir())
            completed += "\\";
        clearCurrentInput();
        insertPlainText(base + completed);
    } else {
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::End);
        setTextCursor(c);
        insertPlainText("\n" + matches.join("    ") + "\n" + currentDir + ">");
        setPromptPos();
        insertPlainText(input);
    }
}

void TerminalEdit::historyUp()
{
    if (history.isEmpty()) return;
    historyIdx = qMin(historyIdx + 1, history.size() - 1);
    clearCurrentInput();
    insertPlainText(history[historyIdx]);
}

void TerminalEdit::historyDown()
{
    if (historyIdx <= 0) {
        historyIdx = -1;
        clearCurrentInput();
        return;
    }
    --historyIdx;
    clearCurrentInput();
    insertPlainText(history[historyIdx]);
}

void TerminalEdit::keyPressEvent(QKeyEvent *e)
{
    ensureCursorInInputZone();

    switch (e->key()) {

    case Qt::Key_Return:
    case Qt::Key_Enter:
        submitCommand();
        return;

    case Qt::Key_Tab:
        handleTab();
        return;

    case Qt::Key_Up:
        historyUp();
        return;

    case Qt::Key_Down:
        historyDown();
        return;

    case Qt::Key_Left:
    case Qt::Key_Backspace:
        if (textCursor().position() <= promptPos) return;
        QPlainTextEdit::keyPressEvent(e);
        return;

    case Qt::Key_Home: {
        QTextCursor c = textCursor();
        bool shift = e->modifiers() & Qt::ShiftModifier;
        c.setPosition(promptPos, shift ? QTextCursor::KeepAnchor
                                         : QTextCursor::MoveAnchor);
        setTextCursor(c);
        return;
    }

    case Qt::Key_C:
        if (e->modifiers() == Qt::ControlModifier) {
            process->write("\x03");
            QTextCursor c = textCursor();
            c.movePosition(QTextCursor::End);
            setTextCursor(c);
            insertPlainText("^C\n");
            setPromptPos();
            return;
        }
        break;

    case Qt::Key_L:
        if (e->modifiers() == Qt::ControlModifier) {
            clear();
            insertPlainText(currentDir + ">");
            setPromptPos();
            return;
        }
        break;

    case Qt::Key_U:
        if (e->modifiers() == Qt::ControlModifier) {
            clearCurrentInput();
            return;
        }
        break;

    default:
        break;
    }

    QPlainTextEdit::keyPressEvent(e);
}

void TerminalEdit::mousePressEvent(QMouseEvent *e)
{
    QPlainTextEdit::mousePressEvent(e);
}

void TerminalEdit::mouseDoubleClickEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseDoubleClickEvent(e);
}

void TerminalEdit::contextMenuEvent(QContextMenuEvent *){}

Terminal::Terminal(QWidget *parent)
    : QWidget(parent)
    , edit(new TerminalEdit(this))
{
    edit->setObjectName("terminalPane");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(edit);
}

void Terminal::setWorkingDirectory(const QString &path){
    edit->setWorkingDirectory(path);
}