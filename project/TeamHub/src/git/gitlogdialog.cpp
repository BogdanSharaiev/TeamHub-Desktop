#include "gitlogdialog.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QTextCursor>
#include <QVBoxLayout>

GitLogDialog::GitLogDialog(GitManager *git, QWidget *parent)
    : QDialog(parent)
    , git(git)
{
    setWindowTitle("Git History");
    resize(980, 640);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(4);

    vbox->addWidget(new QLabel("Double-click a commit to view its diff."));

    auto *splitter = new QSplitter(Qt::Vertical);

    logTable = new QTableWidget(0, 4);
    logTable->setHorizontalHeaderLabels({ "Hash", "Message", "Author", "Date" });
    logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable->setAlternatingRowColors(true);
    logTable->verticalHeader()->setVisible(false);
    logTable->setShowGrid(false);
    logTable->setMinimumHeight(180);
    splitter->addWidget(logTable);

    diffView = new QTextEdit;
    diffView->setReadOnly(true);
    diffView->setObjectName("outputPane");
    diffView->setPlaceholderText("Select a commit to view its diff...");
    diffView->setMinimumHeight(150);
    splitter->addWidget(diffView);

    splitter->setSizes({ 280, 320 });
    vbox->addWidget(splitter, 1);

    setStyleSheet(R"(
        QDialog { background: #1e1e1e; color: #cccccc; }
        QLabel  { color: #aaaaaa; font-size: 11px; }
        QTableWidget {
            background: #252526;
            color: #cccccc;
            border: 1px solid #3c3c3c;
            gridline-color: #3c3c3c;
            selection-background-color: #094771;
        }
        QTableWidget::item:alternate { background: #2a2a2a; }
        QHeaderView::section {
            background: #3c3c3c;
            color: #cccccc;
            border: 1px solid #4c4c4c;
            padding: 4px;
        }
        QSplitter::handle { background: #3c3c3c; }
        QTextEdit#outputPane {
            background: #1e1e1e;
            color: #d4d4d4;
            border: none;
            font-family: Consolas, 'Courier New', monospace;
            font-size: 11px;
        }
    )");

    loadLog();

    connect(logTable, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) {
        if (row < 0) return;
        const QString hash = logTable->item(row, 0)->data(Qt::UserRole).toString();
        applyDiff(diffView, this->git->commitDiff(hash));
    });
}

void GitLogDialog::loadLog()
{
    const auto commits = git->log(100);
    logTable->setRowCount(commits.size());
    for (int i = 0; i < commits.size(); ++i) {
        const auto &c = commits[i];

        auto *hashItem = new QTableWidgetItem(c.shortHash);
        hashItem->setData(Qt::UserRole, c.fullHash);
        hashItem->setForeground(QColor("#569cd6"));
        logTable->setItem(i, 0, hashItem);
        logTable->setItem(i, 1, new QTableWidgetItem(c.message));
        logTable->setItem(i, 2, new QTableWidgetItem(c.author));
        logTable->setItem(i, 3, new QTableWidgetItem(c.date));
    }

    if (commits.isEmpty()) {
        diffView->setPlainText("No commits yet.");
    } else {
        logTable->selectRow(0);
    }
}

void GitLogDialog::applyDiff(QTextEdit *view, const QString &raw)
{
    view->clear();

    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    view->setFont(mono);

    if (raw.isEmpty()) {
        view->setPlainText("No diff available.");
        return;
    }

    QTextCursor cur = view->textCursor();
    cur.movePosition(QTextCursor::Start);

    QTextBlockFormat defaultBlock;
    defaultBlock.setBackground(QColor("#1e1e1e"));

    const QStringList lines = raw.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];

        if (i > 0) cur.insertBlock(defaultBlock);

        QTextBlockFormat blockFmt = defaultBlock;
        QTextCharFormat  charFmt;
        charFmt.setFont(mono);

        if (line.startsWith('+') && !line.startsWith("+++")) {
            blockFmt.setBackground(QColor("#0d2b0d"));
            charFmt.setForeground(QColor("#6db96d"));
        } else if (line.startsWith('-') && !line.startsWith("---")) {
            blockFmt.setBackground(QColor("#2b0d0d"));
            charFmt.setForeground(QColor("#c97070"));
        } else if (line.startsWith("@@")) {
            charFmt.setForeground(QColor("#569cd6"));
        } else if (line.startsWith("diff ") || line.startsWith("index ")
                   || line.startsWith("---") || line.startsWith("+++")) {
            charFmt.setForeground(QColor("#808080"));
        } else {
            charFmt.setForeground(QColor("#cccccc"));
        }

        cur.setBlockFormat(blockFmt);
        cur.insertText(line, charFmt);
    }

    view->moveCursor(QTextCursor::Start);
}
