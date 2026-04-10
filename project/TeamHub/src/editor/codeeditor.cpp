#include "codeeditor.h"

#include <Qsci/qscicommand.h>
#include <Qsci/qscicommandset.h>

#include <QFile>
#include <QTextStream>
#include <QFont>
#include <QColor>
#include <QKeyEvent>
#include <QRegularExpression>

static const QList<QPair<QChar,QChar>> kAutoPairs = {
    {'(', ')'},
    {'[', ']'},
    {'{', '}'},
    {'"', '"'},
    {'\'', '\''},
};

CodeEditor::CodeEditor(QWidget* parent)
    : QsciScintilla(parent)
    , m_lexer(nullptr)
    , m_theme(Theme::Dark)
    , m_zoomLevel(0)
    , m_lintProcess(nullptr)
    , m_lintTimer(nullptr)
{
    setupLexer();
    setupFonts();
    setupMargins();
    setupEditor();
    setupAutoComplete();
    setupLinter();

    connect(this, &QsciScintilla::textChanged,        this, &CodeEditor::fileModified);
    connect(this, &QsciScintilla::cursorPositionChanged, this, &CodeEditor::cursorPositionUpdated);
}

void CodeEditor::setupLexer()
{
    m_lexer = new QsciLexerPython(this);
    applyDarkTheme();
    setLexer(m_lexer);
    setUtf8(true);
}

void CodeEditor::setupFonts()
{
    QFont regular("Consolas", 11);
    regular.setFixedPitch(true);
    regular.setStyleHint(QFont::Monospace);

    QFont italic = regular;
    italic.setItalic(true);

    m_lexer->setDefaultFont(regular);
    for (int s = 0; s <= QsciLexerPython::Inconsistent; ++s)
        m_lexer->setFont(regular, s);

    m_lexer->setFont(italic, QsciLexerPython::Comment);
    m_lexer->setFont(italic, QsciLexerPython::CommentBlock);
}

void CodeEditor::setupMargins()
{
    setMarginType(0, QsciScintilla::NumberMargin);
    setMarginWidth(0, "9999");
    setMarginLineNumbers(0, true);

    setMarginType(1, QsciScintilla::SymbolMargin);
    setMarginWidth(1, 14);
    setMarginSensitivity(1, true);
}

void CodeEditor::setupEditor()
{
    setIndentationsUseTabs(false);
    setIndentationWidth(4);
    setTabWidth(4);
    setTabIndents(true);
    setAutoIndent(true);
    setBackspaceUnindents(true);

    setCaretLineVisible(true);
    setCaretLineBackgroundColor(QColor("#2a2a2a"));
    setCaretWidth(2);

    setBraceMatching(QsciScintilla::SloppyBraceMatch);
    setMatchedBraceBackgroundColor(QColor("#3a3a3a"));
    setMatchedBraceForegroundColor(QColor("#ffd700"));
    setUnmatchedBraceBackgroundColor(QColor("#3a2020"));
    setUnmatchedBraceForegroundColor(QColor("#f44747"));

    setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
    setFoldMarginColors(QColor("#1e1e1e"), QColor("#1e1e1e"));

    setScrollWidth(1);
    setScrollWidthTracking(true);
    setEolMode(QsciScintilla::EolUnix);
    setWrapMode(QsciScintilla::WrapNone);

    setWhitespaceVisibility(QsciScintilla::WsInvisible);
    setWhitespaceForegroundColor(QColor("#3c3c3c"));

    SendScintilla(SCI_SETMULTIPLESELECTION, 1);
    SendScintilla(SCI_SETADDITIONALSELECTIONTYPING, 1);
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
    if (handleBackspaceInPair(event)) return;
    if (skipClosingChar(event))       return;
    if (autoCloseChar(event))         return;

    const Qt::KeyboardModifiers mod = event->modifiers();
    const int key = event->key();

    if (mod == Qt::ControlModifier) {
        if (key == Qt::Key_S)                            { emit fileSaved(); return; }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal) { zoomIn();         return; }
        if (key == Qt::Key_Minus)                        { zoomOut();        return; }
        if (key == Qt::Key_0)                            { resetZoom();      return; }
    }

    QsciScintilla::keyPressEvent(event);
}

bool CodeEditor::autoCloseChar(QKeyEvent* event)
{
    if (event->modifiers() & ~Qt::ShiftModifier)
        return false;

    const QChar ch = event->text().isEmpty() ? QChar() : event->text().at(0);
    if (ch.isNull()) return false;

    for (const auto& [open, close] : kAutoPairs) {
        if (ch != open) continue;

        if (open == close) {
            int line, col;
            getCursorPosition(&line, &col);
            const QString lineText = text(line);
            if (col < lineText.length() && lineText.at(col) == open)
                return false;
        }

        const QString selected = selectedText();
        if (!selected.isEmpty()) {
            replaceSelectedText(QString(open) + selected + close);
        } else {
            QsciScintilla::keyPressEvent(event);
            insert(QString(close));
        }
        return true;
    }
    return false;
}

bool CodeEditor::skipClosingChar(QKeyEvent* event)
{
    if (event->modifiers() != Qt::NoModifier)
        return false;

    const QChar ch = event->text().isEmpty() ? QChar() : event->text().at(0);
    if (ch.isNull()) return false;

    for (const auto& [open, close] : kAutoPairs) {
        if (ch != close || open == close) continue;

        int line, col;
        getCursorPosition(&line, &col);
        const QString lineText = text(line);
        if (col < lineText.length() && lineText.at(col) == close) {
            setCursorPosition(line, col + 1);
            return true;
        }
    }
    return false;
}

bool CodeEditor::handleBackspaceInPair(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Backspace || event->modifiers() != Qt::NoModifier)
        return false;
    if (!selectedText().isEmpty())
        return false;

    int line, col;
    getCursorPosition(&line, &col);
    if (col == 0) return false;

    const QString lineText = text(line);
    const QChar before = lineText.at(col - 1);
    const QChar after  = col < lineText.length() ? lineText.at(col) : QChar();

    for (const auto& [open, close] : kAutoPairs) {
        if (before == open && after == close) {
            setSelection(line, col - 1, line, col + 1);
            removeSelectedText();
            return true;
        }
    }
    return false;
}

void CodeEditor::setupAutoComplete()
{
    QsciAPIs* apis = new QsciAPIs(m_lexer);

    const QStringList keywords = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return",
        "try", "while", "with", "yield",
        "abs", "all", "any", "bool", "breakpoint", "callable", "chr",
        "dict", "dir", "divmod", "enumerate", "eval", "exec", "filter",
        "float", "format", "frozenset", "getattr", "globals", "hasattr",
        "hash", "help", "hex", "id", "input", "int", "isinstance",
        "issubclass", "iter", "len", "list", "locals", "map", "max",
        "min", "next", "object", "oct", "open", "ord", "pow", "print",
        "property", "range", "repr", "reversed", "round", "set", "setattr",
        "slice", "sorted", "staticmethod", "str", "sum", "super", "tuple",
        "type", "vars", "zip", "self", "cls",
        "__init__", "__str__", "__repr__", "__len__", "__getitem__",
        "__setitem__", "__contains__", "__iter__", "__next__",
        "__enter__", "__exit__", "__call__", "__del__",
    };

    for (const auto& kw : keywords)
        apis->add(kw);

    apis->prepare();
    m_lexer->setAPIs(apis);

    setAutoCompletionSource(QsciScintilla::AcsAPIs);
    setAutoCompletionThreshold(2);
    setAutoCompletionCaseSensitivity(false);
    setAutoCompletionReplaceWord(true);
    setAutoCompletionUseSingle(QsciScintilla::AcusExplicit);
    setCallTipsStyle(QsciScintilla::CallTipsNone);
}


void CodeEditor::setupLinter()
{
    indicatorDefine(QsciScintilla::SquiggleIndicator, ErrorIndicator);
    setIndicatorForegroundColor(QColor("#f44747"), ErrorIndicator);

    m_lintTimer = new QTimer(this);
    m_lintTimer->setSingleShot(true);
    m_lintTimer->setInterval(800);

    connect(m_lintTimer, &QTimer::timeout, this, &CodeEditor::checkSyntax);
    connect(this, &QsciScintilla::textChanged, this, [this]() {
        m_lintTimer->start();
    });
}

void CodeEditor::checkSyntax()
{
    if (m_lintProcess && m_lintProcess->state() != QProcess::NotRunning) {
        m_lintProcess->kill();
        m_lintProcess->waitForFinished(200);
    }

    clearIndicatorRange(0, 0, lines(), 0, ErrorIndicator);

    m_lintProcess = new QProcess(this);
    connect(m_lintProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CodeEditor::onLintFinished);

    m_lintProcess->start("python", {
        "-c",
        "import sys,ast\n"
        "src=sys.stdin.buffer.read().decode('utf-8','replace')\n"
        "try:\n"
        "    ast.parse(src,'<editor>')\n"
        "except SyntaxError as e:\n"
        "    print(f'{e.lineno}|{e.offset or 0}|{e.msg}',file=sys.stderr)\n"
    });

    if (m_lintProcess->state() == QProcess::Running) {
        m_lintProcess->write(text().toUtf8());
        m_lintProcess->closeWriteChannel();
    }
}

void CodeEditor::onLintFinished(int exitCode, QProcess::ExitStatus)
{
    if (exitCode == 0 || !m_lintProcess) return;

    const QString err = QString::fromUtf8(m_lintProcess->readAllStandardError()).trimmed();
    if (err.isEmpty()) return;

    static const QRegularExpression re(R"(^(\d+)\|(\d+)\|(.+)$)");
    const auto match = re.match(err);
    if (!match.hasMatch()) return;

    const int line   = match.captured(1).toInt() - 1;
    const int col    = std::max(0, match.captured(2).toInt() - 1);
    const int len    = std::max(1, lineLength(line) - col - 1);

    fillIndicatorRange(line, col, line, col + len, ErrorIndicator);
}

void CodeEditor::loadFile(const QString& filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    setText(in.readAll());
    file.close();

    m_filePath = filepath;
    setModified(false);
}

void CodeEditor::saveFile(const QString& filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << text();
    file.close();

    m_filePath = filepath;
    setModified(false);
    emit fileSaved();
}

void CodeEditor::setFilePath(const QString& filepath) { m_filePath = filepath; }
QString CodeEditor::getFilePath() const               { return m_filePath; }

void CodeEditor::setTheme(Theme theme)
{
    m_theme = theme;
    if (theme == Theme::Dark) applyDarkTheme();
    else                      applyLightTheme();
    setupFonts();
}

void CodeEditor::applyDarkTheme()
{
    if (!m_lexer) return;

    const QColor bg("#1e1e1e");
    const QColor fg("#d4d4d4");

    m_lexer->setDefaultPaper(bg);
    m_lexer->setDefaultColor(fg);
    for (int s = 0; s <= QsciLexerPython::Inconsistent; ++s)
        m_lexer->setPaper(bg, s);

    m_lexer->setColor(fg,                QsciLexerPython::Default);
    m_lexer->setColor(QColor("#569cd6"), QsciLexerPython::Keyword);
    m_lexer->setColor(QColor("#ce9178"), QsciLexerPython::SingleQuotedString);
    m_lexer->setColor(QColor("#ce9178"), QsciLexerPython::DoubleQuotedString);
    m_lexer->setColor(QColor("#ce9178"), QsciLexerPython::TripleSingleQuotedString);
    m_lexer->setColor(QColor("#ce9178"), QsciLexerPython::TripleDoubleQuotedString);
    m_lexer->setColor(QColor("#ce9178"), QsciLexerPython::UnclosedString);
    m_lexer->setColor(QColor("#6a9955"), QsciLexerPython::Comment);
    m_lexer->setColor(QColor("#6a9955"), QsciLexerPython::CommentBlock);
    m_lexer->setColor(QColor("#b5cea8"), QsciLexerPython::Number);
    m_lexer->setColor(QColor("#dcdcaa"), QsciLexerPython::FunctionMethodName);
    m_lexer->setColor(QColor("#4ec9b0"), QsciLexerPython::ClassName);
    m_lexer->setColor(fg,                QsciLexerPython::Operator);
    m_lexer->setColor(fg,                QsciLexerPython::Identifier);
    m_lexer->setColor(QColor("#c586c0"), QsciLexerPython::Decorator);
    m_lexer->setColor(QColor("#9cdcfe"), QsciLexerPython::HighlightedIdentifier);

    setPaper(bg);
    setColor(fg);
    setCaretForegroundColor(QColor("#aeafad"));
    setCaretLineBackgroundColor(QColor("#282828"));
    setSelectionBackgroundColor(QColor("#264f78"));
    setSelectionForegroundColor(QColor("#ffffff"));
    setMarginsBackgroundColor(QColor("#1e1e1e"));
    setMarginsForegroundColor(QColor("#858585"));
    setFoldMarginColors(QColor("#1e1e1e"), QColor("#1e1e1e"));
    setMatchedBraceBackgroundColor(QColor("#3a3a3a"));
    setMatchedBraceForegroundColor(QColor("#ffd700"));
    setUnmatchedBraceBackgroundColor(QColor("#3a2020"));
    setUnmatchedBraceForegroundColor(QColor("#f44747"));
}

void CodeEditor::applyLightTheme()
{
    if (!m_lexer) return;

    const QColor bg("#ffffff");
    const QColor fg("#000000");

    m_lexer->setDefaultPaper(bg);
    m_lexer->setDefaultColor(fg);
    for (int s = 0; s <= QsciLexerPython::Inconsistent; ++s)
        m_lexer->setPaper(bg, s);

    m_lexer->setColor(fg,                QsciLexerPython::Default);
    m_lexer->setColor(QColor("#0000ff"), QsciLexerPython::Keyword);
    m_lexer->setColor(QColor("#a31515"), QsciLexerPython::SingleQuotedString);
    m_lexer->setColor(QColor("#a31515"), QsciLexerPython::DoubleQuotedString);
    m_lexer->setColor(QColor("#a31515"), QsciLexerPython::TripleSingleQuotedString);
    m_lexer->setColor(QColor("#a31515"), QsciLexerPython::TripleDoubleQuotedString);
    m_lexer->setColor(QColor("#a31515"), QsciLexerPython::UnclosedString);
    m_lexer->setColor(QColor("#008000"), QsciLexerPython::Comment);
    m_lexer->setColor(QColor("#008000"), QsciLexerPython::CommentBlock);
    m_lexer->setColor(QColor("#098658"), QsciLexerPython::Number);
    m_lexer->setColor(QColor("#795e26"), QsciLexerPython::FunctionMethodName);
    m_lexer->setColor(QColor("#267f99"), QsciLexerPython::ClassName);
    m_lexer->setColor(fg,                QsciLexerPython::Operator);
    m_lexer->setColor(fg,                QsciLexerPython::Identifier);
    m_lexer->setColor(QColor("#af00db"), QsciLexerPython::Decorator);
    m_lexer->setColor(QColor("#001080"), QsciLexerPython::HighlightedIdentifier);

    setPaper(bg);
    setColor(fg);
    setCaretForegroundColor(QColor("#000000"));
    setCaretLineBackgroundColor(QColor("#f0f0f0"));
    setSelectionBackgroundColor(QColor("#add6ff"));
    setSelectionForegroundColor(QColor("#000000"));
    setMarginsBackgroundColor(QColor("#f3f3f3"));
    setMarginsForegroundColor(QColor("#237893"));
    setFoldMarginColors(QColor("#f3f3f3"), QColor("#f3f3f3"));
    setMatchedBraceBackgroundColor(QColor("#e8e8e8"));
    setMatchedBraceForegroundColor(QColor("#0000cc"));
    setUnmatchedBraceBackgroundColor(QColor("#ffe0e0"));
    setUnmatchedBraceForegroundColor(QColor("#cc0000"));
}

int CodeEditor::currentLine() const
{
    int line, col;
    getCursorPosition(&line, &col);
    return line;
}

int CodeEditor::currentColumn() const
{
    int line, col;
    getCursorPosition(&line, &col);
    return col;
}

bool CodeEditor::isModified() const { return QsciScintilla::isModified(); }

void CodeEditor::undo()    { QsciScintilla::undo(); }
void CodeEditor::redo()    { QsciScintilla::redo(); }

void CodeEditor::zoomIn()
{
    ++m_zoomLevel;
    QsciScintilla::zoomIn();
}

void CodeEditor::zoomOut()
{
    --m_zoomLevel;
    QsciScintilla::zoomOut();
}

void CodeEditor::resetZoom()
{
    zoomTo(0);
    m_zoomLevel = 0;
}
