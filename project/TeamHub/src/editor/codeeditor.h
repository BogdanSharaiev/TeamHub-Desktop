#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <Qsci/qsciapis.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qsciscintilla.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QToolTip>
#include <QWidget>

#include "textsearch.h"

class CodeEditor : public QsciScintilla
{
    Q_OBJECT

public:
    enum class Theme { Dark, Light };

    explicit CodeEditor(QWidget *parent = nullptr);

    void loadFile(const QString &filepath);
    void saveFile(const QString &filepath);
    void setFilePath(const QString &filepath);
    QString getFilePath() const;

    void setTheme(Theme theme);

    bool isModified() const;
    int currentLine() const;
    int currentColumn() const;
    void applyRemoteText(const QString &text);

    void showSearch();
    void hideSearch();

    bool applyingRemote = false;

public slots:
    void resetZoom();
    void onCursorChanged(int line, int index);
    void applyDarkTheme();
    void applyLightTheme();
    void onModified(bool modified);
    void onCharAdded(int ch);

    void onFindNext(const QString &text);
    void onFindPrev(const QString &text);
    void onReplaceOne(const QString &find, const QString &replace);
    void onReplaceAll(const QString &find, const QString &replace);
    void onSearchClosed();
    void repositionSearch();

    void updateRemoteCursor(int siteId, int scintillaPos);
    void removeRemoteCursor(int siteId);
    void clearRemoteCursors();
    void paintRemoteCursors(QWidget *overlay);

    int remoteCursorPos(int siteId) const;
    void goToScintillaPos(int pos);

signals:
    void fileModified();
    void fileSaved();
    void cursorPositionUpdated(int line, int index);
    void modifyChanged(bool modified);
    void localInsert(int position, QChar ch);
    void localDelete(int position);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    struct ErrorInfo
    {
        int line;
        int col;
        QString message;
    };
    QList<ErrorInfo> errorList;

    QMap<int, int> remoteCursorPositions;
    QWidget *cursorOverlay = nullptr;
    static const QColor kCursorColors[4];

    void setupLexer();
    void setupFonts();
    void setupMargins();
    void setupEditor();
    void setupAutoComplete();
    void setupLinter();

    bool autoCloseChar(QKeyEvent *event);
    bool skipClosingChar(QKeyEvent *event);
    bool handleBackspaceInPair(QKeyEvent *event);

    void shiftRemoteCursors(int fromBytePos, int byteDelta);

    void checkSyntax();
    void onLintFinished(int exitCode, QProcess::ExitStatus);

    static constexpr int ErrorIndicator = 8;
    static constexpr int SEARCH_INDICATOR = 9;
    static constexpr int CURRENT_SEARCH_INDICATOR = 10;

    QString filePath;
    QsciLexerPython *lexer;
    Theme theme;
    int zoomLevel;
    QProcess *lintProcess;
    QTimer *lintTimer;

    TextSearch *textSearch;
    void updateSearchHighlights(const QString &text);
    void selectCurrentMatch(const QString &searchText);
    int searchCurrentIndex = 0;
    QList<int> searchMatches;
};

#endif // CODEEDITOR_H
