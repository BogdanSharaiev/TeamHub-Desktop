#include "rgaseq.h"

static bool lessById(const RGAId& a, const RGAId& b)
{
    if (a.timestamp != b.timestamp)
        return a.timestamp < b.timestamp;

    return a.siteId < b.siteId;
}

static bool greaterById(const RGAId& a, const RGAId& b)
{
    if (a.timestamp != b.timestamp)
        return a.timestamp > b.timestamp;

    return a.siteId > b.siteId;
}

static bool isRootId(const RGAId& id)
{
    return id.timestamp == 0 && id.siteId == 0;
}

void RGASequence::insert(const RGANode& node)
{
    if (findById(node.id) != nullptr)
        return;

    int insertIndex = 0;
    bool parentFound = false;

    for (int i = 0; i < rgaseq.size(); ++i) {
        if (rgaseq[i].id == node.parent) {
            insertIndex = i + 1;
            parentFound = true;
            break;
        }
    }

    if (!parentFound && !isRootId(node.parent))
        return;

    auto encodeId = [](const RGAId& id) -> qint64 {
        return ((qint64)id.timestamp << 32) | (quint32)id.siteId;
    };

    QSet<qint64> skippedSet;

    while (insertIndex < rgaseq.size()) {
        const RGANode& curr = rgaseq[insertIndex];

        if (curr.parent == node.parent) {
            if (greaterById(curr.id, node.id)) {
                skippedSet.insert(encodeId(curr.id));
                ++insertIndex;
            } else {
                break;
            }
        } else if (skippedSet.contains(encodeId(curr.parent))) {
            skippedSet.insert(encodeId(curr.id));
            ++insertIndex;
        } else {
            break;
        }
    }

    rgaseq.insert(insertIndex, node);
}

void RGASequence::remove(const RGAId& id)
{
    RGANode* node = findById(id);

    if (!node)
        return;
    node->tombstone = true;
}

QString RGASequence::toText()
{
    QString result;

    for (const RGANode& node : rgaseq) {
        if (!node.tombstone) {
            result.append(node.val);
        }
    }

    return result;
}

RGANode* RGASequence::findById(const RGAId& id)
{
    for (RGANode& node : rgaseq) {
        if (node.id == id) {
            return &node;
        }
    }

    return nullptr;
}

RGAId RGASequence::idAtPosition(int pos)
{
    if (pos < 0)
        return RGAId{};

    int visibleIndex = 0;

    for (const RGANode& node : rgaseq) {
        if (node.tombstone)
            continue;

        if (visibleIndex == pos)
            return node.id;

        ++visibleIndex;
    }

    return RGAId{};
}

int RGASequence::length()
{
    int count = 0;

    for (const RGANode& node : rgaseq) {
        if (!node.tombstone) {
            ++count;
        }
    }

    return count;
}

void RGASequence::clear(){
    rgaseq.clear();
}