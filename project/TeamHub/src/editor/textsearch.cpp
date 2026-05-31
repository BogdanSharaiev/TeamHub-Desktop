#include "textsearch.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

TextSearch::TextSearch(QWidget* parent)
    :QWidget(parent)
{
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* firstLine = new QHBoxLayout();
    findLine = new QLineEdit(this);
    findLine->setPlaceholderText("Find...");
    prev = new QPushButton("<- Prev", this);
    next = new QPushButton("Next ->", this);
    close = new QPushButton("x", this);
    firstLine->addWidget(findLine);
    firstLine->addWidget(prev);
    firstLine->addWidget(next);
    firstLine->addWidget(close);
    QHBoxLayout* secondLine = new QHBoxLayout();
    replaceLine = new QLineEdit(this);
    replaceLine->setPlaceholderText("Replace...");
    replace = new QPushButton("Replace", this);
    replaceAll = new QPushButton("Replace All", this);
    secondLine->addWidget(replaceLine);
    secondLine->addWidget(replace);
    secondLine->addWidget(replaceAll);
    mainLayout->addLayout(firstLine);
    mainLayout->addLayout(secondLine);

    setObjectName("textSearch");

    findLine->setObjectName("searchEdit");
    replaceLine->setObjectName("searchEdit");

    prev->setObjectName("searchNavBtn");
    next->setObjectName("searchNavBtn");
    close->setObjectName("searchNavBtn");

    replace->setObjectName("searchReplaceBtn");
    replaceAll->setObjectName("searchReplaceBtn");

    connect(close, &QPushButton::clicked, this, &TextSearch::onClose);
}

void TextSearch::onClose(){
    this->hide();
}
