#ifndef RGASEQ_H
#define RGASEQ_H

#include "rganode.h"
#include <QList>
#include <QString>

class RGASequence{
public:
    void insert(const RGANode& node);
    void remove(const RGAId& id);
    QString toText();
    RGANode* findById(const RGAId& id);
    RGAId idAtPosition(int pos);
    int length();
    void clear();

// private:
    QList<RGANode> rgaseq;
};

#endif // RGASEQ_H
