#ifndef TEXTSEARCH_H
#define TEXTSEARCH_H

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

class TextSearch : public QWidget
{
    Q_OBJECT

public:
    explicit TextSearch(QWidget *parent = nullptr);

    void focusFind();

    QLineEdit *getFindLine() const { return findLine; }
    QLineEdit *getReplaceLine() const { return replaceLine; }

signals:
    void findNext(const QString &text);
    void findPrev(const QString &text);
    void replaceOne(const QString &find, const QString &replace);
    void replaceAllText(const QString &find, const QString &replace);
    void closed();
    void searchTextChanged(const QString&);

protected:
    void keyPressEvent(QKeyEvent *event) override;

public slots:
    void updateMatchLabel(int current, int total);


private slots:
    void onClose();
    void onFindNext();
    void onFindPrev();
    void onReplace();
    void onReplaceAll();

private:
    QLineEdit *findLine;
    QLineEdit *replaceLine;
    QLabel *matchLabel;
    QPushButton *prev;
    QPushButton *next;
    QPushButton *replace;
    QPushButton *replaceAll;
    QPushButton *closeBtn;
};

#endif // TEXTSEARCH_H