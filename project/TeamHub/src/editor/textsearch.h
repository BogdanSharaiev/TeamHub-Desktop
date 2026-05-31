#ifndef TEXTSEARCH_H
#define TEXTSEARCH_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class TextSearch : public QWidget
{
    Q_OBJECT
public:
    explicit TextSearch(QWidget* parent = nullptr);

private slots:
    void onClose();

private:
    QLineEdit* findLine;
    QLineEdit* replaceLine;
    QLabel* matchLines;
    QPushButton* prev;
    QPushButton* next;
    QPushButton* replace;
    QPushButton* replaceAll;
    QPushButton* close;

};

#endif // TEXTSEARCH_H
