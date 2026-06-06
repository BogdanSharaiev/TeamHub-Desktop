#ifndef RGASEQ_H
#define RGASEQ_H

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include "rganode.h"

class RGASequence
{
public:
    void insert(const RGANode &node);
    void remove(const RGAId &id);
    QString toText();
    RGANode *findById(const RGAId &id);
    RGAId idAtPosition(int pos);
    int length();
    void clear();
    void rebuildIndex();
    void undelete(const RGAId &id);

    static qint64 encodeId(const RGAId &id)
    {
        return (static_cast<qint64>(id.timestamp) << 32) | static_cast<quint32>(id.siteId);
    }

    // private:
    QList<RGANode> rgaseq;

private:
    QHash<qint64, int> idIndex;
};

#endif // RGASEQ_H
