#include "codeeditor.h"

#include <Qsci/qscicommand.h>
#include <Qsci/qscicommandset.h>

#include <QColor>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextStream>
#include <QDir>

static const QList<QPair<QChar, QChar>> kAutoPairs = {
    {'(', ')'},
    {'[', ']'},
    {'{', '}'},
    {'"', '"'},
    {'\'', '\''},
};

CodeEditor::CodeEditor(QWidget *parent)
    : QsciScintilla(parent)
    , lexer(nullptr)
    , theme(Theme::Dark)
    , zoomLevel(0)
    , lintProcess(nullptr)
    , lintTimer(nullptr)
{
    setupLexer();
    setupFonts();
    setupMargins();
    setupEditor();
    setupAutoComplete();
    setupLinter();
    textSearch = new TextSearch(this);
    textSearch->hide();

    connect(textSearch, &TextSearch::findNext, this, &CodeEditor::onFindNext);
    connect(textSearch, &TextSearch::findPrev, this, &CodeEditor::onFindPrev);
    connect(textSearch, &TextSearch::replaceOne, this, &CodeEditor::onReplaceOne);
    connect(textSearch, &TextSearch::replaceAllText, this, &CodeEditor::onReplaceAll);
    connect(textSearch, &TextSearch::closed, this, &CodeEditor::onSearchClosed);
    connect(textSearch, &TextSearch::searchTextChanged, this, &CodeEditor::updateSearchHighlights);

    connect(this, SIGNAL(textChanged()), this, SIGNAL(fileModified()));
    connect(this, SIGNAL(cursorPositionChanged(int, int)), this, SLOT(onCursorChanged(int, int)));
    connect(this, SIGNAL(modificationChanged(bool)), this, SLOT(onModified(bool)));
}

void CodeEditor::setupLexer()
{
    lexer = new QsciLexerPython(this);
    applyDarkTheme();
    setLexer(lexer);
    setUtf8(true);
}

void CodeEditor::setupFonts()
{
    QFont regular("Consolas", 11);
    regular.setFixedPitch(true);
    regular.setStyleHint(QFont::Monospace);

    QFont italic = regular;
    italic.setItalic(true);

    lexer->setDefaultFont(regular);
    for (int s = 0; s <= QsciLexerPython::Inconsistent; ++s)
        lexer->setFont(regular, s);

    lexer->setFont(italic, QsciLexerPython::Comment);
    lexer->setFont(italic, QsciLexerPython::CommentBlock);
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

void CodeEditor::onCursorChanged(int line, int index)
{
    emit cursorPositionUpdated(line, index);
}

void CodeEditor::onModified(bool modified)
{
    emit modifyChanged(modified);
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (applyingRemote) {
        QsciScintilla::keyPressEvent(event);
        return;
    }

    if (handleBackspaceInPair(event))
        return;
    if (skipClosingChar(event))
        return;
    if (autoCloseChar(event))
        return;

    const Qt::KeyboardModifiers mod = event->modifiers();
    const int key = event->key();

    if (mod == Qt::ControlModifier) {
        if (mod == Qt::ControlModifier && key == Qt::Key_F) {
            showSearch();
            return;
        }
        if (key == Qt::Key_S) {
            if (!filePath.isEmpty())
                saveFile(filePath);
            return;
        }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
            zoomIn();
            return;
        }
        if (key == Qt::Key_Minus) {
            zoomOut();
            return;
        }
        if (key == Qt::Key_0) {
            resetZoom();
            return;
        }
    }

    if (mod == Qt::NoModifier || mod == Qt::ShiftModifier) {
        QString txt = event->text();
        if (!txt.isEmpty()) {
            QChar ch = txt.at(0);
            if (ch.isPrint() || ch == '\n' || ch == '\r') {
                int pos = SendScintilla(SCI_GETCURRENTPOS);
                QChar sendChar = (ch == '\r') ? QChar('\n') : ch;
                QsciScintilla::keyPressEvent(event);
                emit localInsert(pos, sendChar);

                if (sendChar == '\n') {
                    int newPos = SendScintilla(SCI_GETCURRENTPOS);
                    int indentLen = newPos - (pos + 1);
                    if (indentLen > 0) {
                        QString fullText = text();
                        for (int i = 0; i < indentLen; i++) {
                            emit localInsert(pos + 1 + i, fullText.at(pos + 1 + i));
                        }
                    }
                }
                return;
            }
        }

        if (key == Qt::Key_Backspace && mod == Qt::NoModifier) {
            int pos = SendScintilla(SCI_GETCURRENTPOS);
            if (pos > 0) {
                QsciScintilla::keyPressEvent(event);
                emit localDelete(pos - 1);
                return;
            }
        }

        if (key == Qt::Key_Delete && mod == Qt::NoModifier) {
            int pos = SendScintilla(SCI_GETCURRENTPOS);
            QsciScintilla::keyPressEvent(event);
            emit localDelete(pos);
            return;
        }
    }

    QsciScintilla::keyPressEvent(event);
}

bool CodeEditor::autoCloseChar(QKeyEvent *event)
{
    if (event->modifiers() & ~Qt::ShiftModifier)
        return false;

    const QChar ch = event->text().isEmpty() ? QChar() : event->text().at(0);
    if (ch.isNull())
        return false;

    for (const auto &[open, close] : kAutoPairs) {
        if (ch != open)
            continue;

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
            int pos = SendScintilla(SCI_GETCURRENTPOS);
            QsciScintilla::keyPressEvent(event);
            insert(QString(close));

            emit localInsert(pos, open);
            emit localInsert(pos + 1, close);
        }
        return true;
    }
    return false;
}

bool CodeEditor::skipClosingChar(QKeyEvent *event)
{
    if (event->modifiers() != Qt::NoModifier)
        return false;

    const QChar ch = event->text().isEmpty() ? QChar() : event->text().at(0);
    if (ch.isNull())
        return false;

    for (const auto &[open, close] : kAutoPairs) {
        if (ch != close || open == close)
            continue;

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

bool CodeEditor::handleBackspaceInPair(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Backspace || event->modifiers() != Qt::NoModifier)
        return false;
    if (!selectedText().isEmpty())
        return false;

    int line, col;
    getCursorPosition(&line, &col);
    if (col == 0)
        return false;

    const QString lineText = text(line);
    const QChar before = lineText.at(col - 1);
    const QChar after = col < lineText.length() ? lineText.at(col) : QChar();

    for (const auto &[open, close] : kAutoPairs) {
        if (before == open && after == close) {
            int pos = SendScintilla(SCI_GETCURRENTPOS);
            setSelection(line, col - 1, line, col + 1);
            removeSelectedText();
            emit localDelete(pos);
            emit localDelete(pos - 1);
            return true;
        }
    }
    return false;
}

void CodeEditor::setupAutoComplete()
{
    QsciAPIs *apis = new QsciAPIs(lexer);

    const QStringList keywords = {
        "False",        "None",       "True",         "and",        "as",          "assert",
        "async",        "await",      "break",        "class",      "continue",    "def",
        "del",          "elif",       "else",         "except",     "finally",     "for",
        "from",         "global",     "if",           "import",     "in",          "is",
        "lambda",       "nonlocal",   "not",          "or",         "pass",        "raise",
        "return",       "try",        "while",        "with",       "yield",       "abs",
        "all",          "any",        "bool",         "breakpoint", "callable",    "chr",
        "dict",         "dir",        "divmod",       "enumerate",  "eval",        "exec",
        "filter",       "float",      "format",       "frozenset",  "getattr",     "globals",
        "hasattr",      "hash",       "help",         "hex",        "id",          "input",
        "int",          "isinstance", "issubclass",   "iter",       "len",         "list",
        "locals",       "map",        "max",          "min",        "next",        "object",
        "oct",          "open",       "ord",          "pow",        "print",       "property",
        "range",        "repr",       "reversed",     "round",      "set",         "setattr",
        "slice",        "sorted",     "staticmethod", "str",        "sum",         "super",
        "tuple",        "type",       "vars",         "zip",        "self",        "cls",
        "__init__",     "__str__",    "__repr__",     "__len__",    "__getitem__", "__setitem__",
        "__contains__", "__iter__",   "__next__",     "__enter__",  "__exit__",    "__call__",
        "__del__",
    };

    for (const auto &kw : keywords)
        apis->add(kw);

    apis->prepare();
    lexer->setAPIs(apis);

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

    indicatorDefine(QsciScintilla::BoxIndicator, SEARCH_INDICATOR);
    setIndicatorForegroundColor(QColor("#d7ba7d"), SEARCH_INDICATOR);
    setIndicatorOutlineColor(QColor("#d7ba7d"), SEARCH_INDICATOR);

    lintTimer = new QTimer(this);
    lintTimer->setSingleShot(true);
    lintTimer->setInterval(800);

    connect(lintTimer, &QTimer::timeout,
            this, &CodeEditor::checkSyntax);

    connect(this, SIGNAL(textChanged()),
            lintTimer, SLOT(start()));
}

void CodeEditor::checkSyntax()
{
    if (lintProcess) {
        if (lintProcess->state() != QProcess::NotRunning) {
            lintProcess->kill();
            lintProcess->waitForFinished(300);
        }
        delete lintProcess;
        lintProcess = nullptr;
    }

    clearIndicatorRange(0, 0, lines(), 0, ErrorIndicator);

    QString tmpPath = QDir::tempPath() + "/teamhub_lint_tmp.py";
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[Linter] Cannot write temp file:" << tmpPath;
        return;
    }
    QTextStream stream(&tmpFile);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text();
    tmpFile.close();

    lintProcess = new QProcess(this);
    lintProcess->setProcessChannelMode(QProcess::SeparateChannels);

    connect(lintProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CodeEditor::onLintFinished);

    qDebug() << "[Linter] Running pyflakes on:" << tmpPath;
    lintProcess->start("python", {"-m", "pyflakes", tmpPath});

    if (!lintProcess->waitForStarted(3000)) {
        qDebug() << "[Linter] Failed to start:" << lintProcess->errorString();
        delete lintProcess;
        lintProcess = nullptr;
        QFile::remove(tmpPath);
        return;
    }
}

void CodeEditor::onLintFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (!lintProcess) return;

    const QString out = QString::fromUtf8(
                            lintProcess->readAllStandardOutput()).trimmed();
    const QString err = QString::fromUtf8(
                            lintProcess->readAllStandardError()).trimmed();

    qDebug() << "[Linter] exitCode:" << exitCode;
    qDebug() << "[Linter] stdout:" << out;
    qDebug() << "[Linter] stderr:" << err;

    lintProcess->deleteLater();
    lintProcess = nullptr;

    QString tmpPath = QDir::tempPath() + "/teamhub_lint_tmp.py";
    QFile::remove(tmpPath);

    QString combined;
    if (!out.isEmpty() && !err.isEmpty())
        combined = out + "\n" + err;
    else
        combined = out.isEmpty() ? err : out;

    if (combined.isEmpty()) {
        qDebug() << "[Linter] No output";
        return;
    }

    static const QRegularExpression re(
        R"([^:]+:(\d+):(\d+)[: ].+)");

    int matchCount = 0;
    for (const QString& rawLine : combined.split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        qDebug() << "[Linter] Checking:" << line;

        const auto match = re.match(line);
        if (!match.hasMatch()) {
            qDebug() << "[Linter] No match:" << line;
            continue;
        }

        const int ln  = match.captured(1).toInt() - 1;
        const int col = std::max(0, match.captured(2).toInt() - 1);

        if (ln < 0 || ln >= lines()) {
            qDebug() << "[Linter] Out of range:" << ln;
            continue;
        }

        const int len = std::max(1, lineLength(ln) - col - 1);
        qDebug() << "[Linter] Highlight ln=" << ln << "col=" << col << "len=" << len;
        fillIndicatorRange(ln, col, ln, col + len, ErrorIndicator);
        matchCount++;
    }

    qDebug() << "[Linter] Total:" << matchCount;
}

void CodeEditor::loadFile(const QString &filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    setText(in.readAll());
    file.close();

    filePath = filepath;
    setModified(false);
}

void CodeEditor::saveFile(const QString &filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << text();
    file.close();

    filePath = filepath;
    setModified(false);
    emit fileSaved();
}

void CodeEditor::setFilePath(const QString &filepath)
{
    filePath = filepath;
}
QString CodeEditor::getFilePath() const
{
    return filePath;
}

void CodeEditor::setTheme(Theme t)
{
    theme = t;
    if (theme == Theme::Dark)
        applyDarkTheme();
    else
        applyLightTheme();
    setupFonts();
}

void CodeEditor::applyDarkTheme()
{
    if (!lexer)
        return;

    const QColor bg("#1e1e1e");
    const QColor fg("#d4d4d4");

    lexer->setDefaultPaper(bg);
    lexer->setDefaultColor(fg);
    for (int s = 0; s <= QsciLexerPython::Inconsistent; ++s)
        lexer->setPaper(bg, s);

    lexer->setColor(fg, QsciLexerPython::Default);
    lexer->setColor(QColor("#569cd6"), QsciLexerPython::Keyword);
    lexer->setColor(QColor("#ce9178"), QsciLexerPython::SingleQuotedString);
    lexer->setColor(QColor("#ce9178"), QsciLexerPython::DoubleQuotedString);
    lexer->setColor(QColor("#ce9178"), QsciLexerPython::TripleSingleQuotedString);
    lexer->setColor(QColor("#ce9178"), QsciLexerPython::TripleDoubleQuotedString);
    lexer->setColor(QColor("#ce9178"), QsciLexerPython::UnclosedString);
    lexer->setColor(QColor("#6a9955"), QsciLexerPython::Comment);
    lexer->setColor(QColor("#6a9955"), QsciLexerPython::CommentBlock);
    lexer->setColor(QColor("#b5cea8"), QsciLexerPython::Number);
    lexer->setColor(QColor("#dcdcaa"), QsciLexerPython::FunctionMethodName);
    lexer->setColor(QColor("#4ec9b0"), QsciLexerPython::ClassName);
    lexer->setColor(fg, QsciLexerPython::Operator);
    lexer->setColor(fg, QsciLexerPython::Identifier);
    lexer->setColor(QColor("#c586c0"), QsciLexerPython::Decorator);
    lexer->setColor(QColor("#9cdcfe"), QsciLexerPython::HighlightedIdentifier);

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
    if (!lexer)
        return;

    const QColor bg("#ffffff");
    const QColor fg("#000000");

    lexer->setDefaultPaper(bg);
    lexer->setDefaultColor(fg);
    for (int s = 0; s <= QsciLexerPython::Inconsistent; ++s)
        lexer->setPaper(bg, s);

    lexer->setColor(fg, QsciLexerPython::Default);
    lexer->setColor(QColor("#0000ff"), QsciLexerPython::Keyword);
    lexer->setColor(QColor("#a31515"), QsciLexerPython::SingleQuotedString);
    lexer->setColor(QColor("#a31515"), QsciLexerPython::DoubleQuotedString);
    lexer->setColor(QColor("#a31515"), QsciLexerPython::TripleSingleQuotedString);
    lexer->setColor(QColor("#a31515"), QsciLexerPython::TripleDoubleQuotedString);
    lexer->setColor(QColor("#a31515"), QsciLexerPython::UnclosedString);
    lexer->setColor(QColor("#008000"), QsciLexerPython::Comment);
    lexer->setColor(QColor("#008000"), QsciLexerPython::CommentBlock);
    lexer->setColor(QColor("#098658"), QsciLexerPython::Number);
    lexer->setColor(QColor("#795e26"), QsciLexerPython::FunctionMethodName);
    lexer->setColor(QColor("#267f99"), QsciLexerPython::ClassName);
    lexer->setColor(fg, QsciLexerPython::Operator);
    lexer->setColor(fg, QsciLexerPython::Identifier);
    lexer->setColor(QColor("#af00db"), QsciLexerPython::Decorator);
    lexer->setColor(QColor("#001080"), QsciLexerPython::HighlightedIdentifier);

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

bool CodeEditor::isModified() const
{
    return QsciScintilla::isModified();
}

void CodeEditor::resetZoom()
{
    zoomTo(0);
    zoomLevel = 0;
}

void CodeEditor::onCharAdded(int ch)
{
    int pos = SendScintilla(SCI_GETCURRENTPOS);

    if (ch == 0)
        return;

    QChar addedChar = QChar(ch);

    if (addedChar.isPrint()) {
        emit localInsert(pos, addedChar);
    }
}

void CodeEditor::applyRemoteText(const QString &newText)
{
    applyingRemote = true;
    blockSignals(true);

    int oldPos = SendScintilla(SCI_GETCURRENTPOS);
    setText(newText);
    SendScintilla(SCI_CLEARSELECTIONS);
    SendScintilla(SCI_SETSELECTION, -1, -1);

    int maxPos = SendScintilla(SCI_GETTEXTLENGTH);
    int newPos = qMin(oldPos, maxPos);

    SendScintilla(SCI_SETSEL, newPos, newPos);

    SendScintilla(SCI_SCROLLCARET);
    update();

    blockSignals(false);
    applyingRemote = false;
}

void CodeEditor::showSearch()
{
    repositionSearch();
    textSearch->show();
    textSearch->raise();
    textSearch->focusFind();
}

void CodeEditor::hideSearch()
{
    textSearch->hide();
    setFocus();
    clearIndicatorRange(0, 0, lines(), 0, SEARCH_INDICATOR);
    searchMatches.clear();
}

void CodeEditor::repositionSearch()
{
    const int margin = 8;
    const int w = 520;
    const int h = textSearch->sizeHint().height() + 10;
    textSearch->setFixedWidth(w);
    int x = width() - w - margin;
    int y = margin;
    textSearch->setGeometry(x, y, w, h);
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QsciScintilla::resizeEvent(event);
    if (textSearch->isVisible())
        repositionSearch();
}

void CodeEditor::onSearchClosed()
{
    setFocus();
    clearIndicatorRange(0, 0, lines(), 0, SEARCH_INDICATOR);
    searchMatches.clear();
}

void CodeEditor::updateSearchHighlights(const QString &searchText)
{
    clearIndicatorRange(0, 0, lines(), 0, SEARCH_INDICATOR);

    searchMatches.clear();

    if (searchText.isEmpty())
        return;

    const QString src = text();

    int pos = 0;

    while ((pos = src.indexOf(searchText, pos, Qt::CaseInsensitive)) != -1) {
        searchMatches.append(pos);
        pos += searchText.length();
    }

    for (int p : searchMatches) {
        int line, col;
        lineIndexFromPosition(p, &line, &col);

        int eline, ecol;
        lineIndexFromPosition(p + searchText.length(), &eline, &ecol);

        fillIndicatorRange(line, col, eline, ecol, SEARCH_INDICATOR);
    }

    searchCurrentIndex = -1;
}

void CodeEditor::onFindNext(const QString &searchText)
{
    if (searchText.isEmpty())
        return;

    if (searchMatches.isEmpty())
        updateSearchHighlights(searchText);

    if (searchMatches.isEmpty())
        return;

    searchCurrentIndex = (searchCurrentIndex + 1) % searchMatches.size();

    selectCurrentMatch(searchText);
}

void CodeEditor::onFindPrev(const QString &searchText)
{
    if (searchText.isEmpty())
        return;

    if (searchMatches.isEmpty())
        updateSearchHighlights(searchText);

    if (searchMatches.isEmpty())
        return;

    searchCurrentIndex = (searchCurrentIndex - 1 + searchMatches.size()) % searchMatches.size();

    selectCurrentMatch(searchText);
}

void CodeEditor::onReplaceOne(const QString &findText, const QString &replaceText)
{
    if (findText.isEmpty())
        return;
    if (selectedText().compare(findText, Qt::CaseInsensitive) == 0)
        replaceSelectedText(replaceText);
    onFindNext(findText);
}

void CodeEditor::onReplaceAll(const QString &findText, const QString &replaceText)
{
    if (findText.isEmpty())
        return;
    QString src = text();
    src.replace(findText, replaceText, Qt::CaseInsensitive);
    setText(src);
    clearIndicatorRange(0, 0, lines(), 0, SEARCH_INDICATOR);
    searchMatches.clear();
}

void CodeEditor::selectCurrentMatch(const QString& searchText)
{
    if (searchMatches.isEmpty())
        return;

    int pos = searchMatches[searchCurrentIndex];

    int line, col;
    lineIndexFromPosition(pos, &line, &col);

    int eline, ecol;
    lineIndexFromPosition(pos + searchText.length(), &eline, &ecol);

    setSelection(line, col, eline, ecol);

    ensureLineVisible(line);

    textSearch->updateMatchLabel(searchCurrentIndex + 1,
                                 searchMatches.size());
}