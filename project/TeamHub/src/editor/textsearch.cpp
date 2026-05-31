#include "textsearch.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

TextSearch::TextSearch(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(5);

    auto* firstLine = new QHBoxLayout();
    firstLine->setSpacing(4);

    findLine = new QLineEdit(this);
    findLine->setPlaceholderText("Find...");
    findLine->setObjectName("searchEdit");

    matchLabel = new QLabel("", this);
    matchLabel->setObjectName("stubLabel");
    matchLabel->setMinimumWidth(50);
    matchLabel->setAlignment(Qt::AlignCenter);

    prev      = new QPushButton("← Prev", this);
    next      = new QPushButton("Next →", this);
    closeBtn  = new QPushButton("✕", this);
    closeBtn->setFixedWidth(28);

    firstLine->addWidget(findLine, 1);
    firstLine->addWidget(matchLabel);
    firstLine->addWidget(prev);
    firstLine->addWidget(next);
    firstLine->addWidget(closeBtn);

    auto* secondLine = new QHBoxLayout();
    secondLine->setSpacing(4);

    replaceLine = new QLineEdit(this);
    replaceLine->setPlaceholderText("Replace...");
    replaceLine->setObjectName("searchEdit");

    replace    = new QPushButton("Replace", this);
    replaceAll = new QPushButton("Replace All", this);

    secondLine->addWidget(replaceLine, 1);
    secondLine->addWidget(replace);
    secondLine->addWidget(replaceAll);

    mainLayout->addLayout(firstLine);
    mainLayout->addLayout(secondLine);

    setObjectName("textSearch");
    prev->setObjectName("searchNavBtn");
    next->setObjectName("searchNavBtn");
    closeBtn->setObjectName("searchNavBtn");
    replace->setObjectName("searchReplaceBtn");
    replaceAll->setObjectName("searchReplaceBtn");

    connect(closeBtn,   &QPushButton::clicked, this, &TextSearch::onClose);
    connect(next,       &QPushButton::clicked, this, &TextSearch::onFindNext);
    connect(prev,       &QPushButton::clicked, this, &TextSearch::onFindPrev);
    connect(replace,    &QPushButton::clicked, this, &TextSearch::onReplace);
    connect(replaceAll, &QPushButton::clicked, this, &TextSearch::onReplaceAll);
    connect(findLine, &QLineEdit::textChanged, this, &TextSearch::searchTextChanged);

    connect(findLine, &QLineEdit::returnPressed, this, &TextSearch::onFindNext);
}

void TextSearch::focusFind()
{
    findLine->setFocus();
    findLine->selectAll();
}

void TextSearch::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        onClose();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TextSearch::onClose()
{
    hide();
    emit closed();
}

void TextSearch::onFindNext()
{
    const QString t = findLine->text();
    if (!t.isEmpty()) emit findNext(t);
}

void TextSearch::onFindPrev()
{
    const QString t = findLine->text();
    if (!t.isEmpty()) emit findPrev(t);
}

void TextSearch::onReplace()
{
    emit replaceOne(findLine->text(), replaceLine->text());
}

void TextSearch::onReplaceAll()
{
    emit replaceAllText(findLine->text(), replaceLine->text());
}

void TextSearch::updateMatchLabel(int current, int total)
{
    if (total == 0)
        matchLabel->setText("no matches");
    else
        matchLabel->setText(QString("%1 / %2").arg(current).arg(total));
}