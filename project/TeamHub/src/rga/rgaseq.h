#ifndef RGASEQ_H
#define RGASEQ_H

#include "rganode.h"
#include <QList>
#include <QString>

class RGASequence{
public:
    void insert(RGANode& node);
    void remove(RGAId& id);
    QString toText();
    RGANode* findById(RGAId& id);
    RGAId idAtPosition(int pos);
    int length();

private:
    QList<RGANode> rgaseq;
};

#endif // RGASEQ_H
