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
    void undo();
    void redo();
    void zoomIn();
    void zoomOut();
    void resetZoom();

    void applyDarkTheme();
    void applyLightTheme();

signals:
    void fileModified();
    void fileSaved();
    void cursorPositionUpdated(int line, int col);

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

    QString           m_filePath;
    QsciLexerPython*  m_lexer;
    Theme             m_theme;
    int               m_zoomLevel;
    QProcess*         m_lintProcess;
    QTimer*           m_lintTimer;
};

#endif // CODEEDITOR_H
