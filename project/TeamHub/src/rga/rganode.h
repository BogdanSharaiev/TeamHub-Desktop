#ifndef RGANODE_H
#define RGANODE_H

#include <QChar>

struct RGAId{
    int timestamp = 0;
    int siteId = 0;

    bool operator==(const RGAId& other) const{
        return timestamp == other.timestamp && siteId == other.siteId;
    }
    bool operator!=(const RGAId& other) const{
        return timestamp != other.timestamp || siteId != other.siteId;
    }
};

struct RGANode{
    RGAId id;
    RGAId parent;
    QChar val;
    bool tombstone = false;

    bool operator==(const RGANode& other) const{
        return id == other.id;
    }
};

#endif // RGANODE_H
