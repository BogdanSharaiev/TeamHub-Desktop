#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qsciapis.h>

#include <QWidget>
#include <QString>
#include <QKeyEvent>
#include <QProcess>
#include <QTimer>

class CodeEditor : public QsciScintilla
{
    Q_OBJECT

public:
    enum class Theme { Dark, Light };

    explicit CodeEditor(QWidget* parent = nullptr);

    void loadFile(const QString& filepath);
    void saveFile(const QString& filepath);
    void setFilePath(const QString& filepath);
    QString getFilePath() const;

    void setTheme(Theme theme);

    bool isModified() const;
    int  currentLine() const;
    int  currentColumn() const;

public slots:
    void resetZoom();
    void onCursorChanged(int line, int index);
    void applyDarkTheme();
    void applyLightTheme();
    void onModified(bool modified);
    void onCharAdded(int ch);

signals:
    void fileModified();
    void fileSaved();
    void cursorPositionUpdated(int line, int index);
    void modifyChanged(bool modified);
    void localInsert(int position, QChar ch);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupLexer();
    void setupFonts();
    void setupMargins();
    void setupEditor();
    void setupAutoComplete();
    void setupLinter();

    bool autoCloseChar(QKeyEvent* event);
    bool skipClosingChar(QKeyEvent* event);
    bool handleBackspaceInPair(QKeyEvent* event);

    void checkSyntax();
    void onLintFinished(int exitCode, QProcess::ExitStatus);

    static constexpr int ErrorIndicator = 8;

    QString           filePath;
    QsciLexerPython*  lexer;
    Theme             theme;
    int               zoomLevel;
    QProcess*         lintProcess;
    QTimer*           lintTimer;
};

#endif // CODEEDITOR_H
