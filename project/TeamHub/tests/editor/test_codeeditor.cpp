#include <QTest>
#include <QSignalSpy>
#include <QTemporaryFile>

#include "../../src/editor/codeeditor.h"

class TestCodeEditor : public QObject
{
    Q_OBJECT

private slots:
    void init()    { m_editor = new CodeEditor(); }
    void cleanup() { delete m_editor; }

    void testInitialTextIsEmpty();
    void testSetAndGetText();
    void testIsModifiedAfterSetText();
    void testFilePathGetSet();
    void testInitialCursorPosition();
    void testThemeSwitchDoesNotCrash();
    void testSaveAndLoadFile();
    void testFileSavedSignal();

private:
    CodeEditor* m_editor = nullptr;
};

void TestCodeEditor::testInitialTextIsEmpty()
{
    QVERIFY(m_editor->text().isEmpty());
}

void TestCodeEditor::testSetAndGetText()
{
    m_editor->setText("x = 1\n");
    QCOMPARE(m_editor->text(), QString("x = 1\n"));
}

void TestCodeEditor::testIsModifiedAfterSetText()
{
    m_editor->setText("x = 42");
    QVERIFY(m_editor->isModified());
}

void TestCodeEditor::testFilePathGetSet()
{
    const QString path = "/project/script.py";
    m_editor->setFilePath(path);
    QCOMPARE(m_editor->getFilePath(), path);
}

void TestCodeEditor::testInitialCursorPosition()
{
    QCOMPARE(m_editor->currentLine(),   0);
    QCOMPARE(m_editor->currentColumn(), 0);
}

void TestCodeEditor::testThemeSwitchDoesNotCrash()
{
    m_editor->setTheme(CodeEditor::Theme::Dark);
    m_editor->setTheme(CodeEditor::Theme::Light);
    m_editor->setTheme(CodeEditor::Theme::Dark);
    QVERIFY(true);
}

void TestCodeEditor::testSaveAndLoadFile()
{
    const QString content = "def hello():\n    pass\n";
    m_editor->setText(content);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    const QString path = tmp.fileName();
    tmp.close();

    m_editor->saveFile(path);
    QVERIFY(!m_editor->isModified());

    CodeEditor loader;
    loader.loadFile(path);
    QCOMPARE(loader.text(), content);
    QVERIFY(!loader.isModified());
}

void TestCodeEditor::testFileSavedSignal()
{
    QSignalSpy spy(m_editor, &CodeEditor::fileSaved);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    const QString path = tmp.fileName();
    tmp.close();

    m_editor->saveFile(path);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestCodeEditor)
#include "test_codeeditor.moc"
