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
    const qint64 enc = encodeId(node.id);
    if (idIndex.contains(enc))
        return;

    int insertIndex = 0;

    if (!isRootId(node.parent)) {
        auto pit = idIndex.find(encodeId(node.parent));
        if (pit == idIndex.end())
            return;
        insertIndex = pit.value() + 1;
    }

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

    for (auto it = idIndex.begin(); it != idIndex.end(); ++it) {
        if (it.value() >= insertIndex)
            ++it.value();
    }
    idIndex.insert(enc, insertIndex);
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
    auto it = idIndex.find(encodeId(id));
    if (it == idIndex.end())
        return nullptr;
    return &rgaseq[it.value()];
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

void RGASequence::clear()
{
    rgaseq.clear();
    idIndex.clear();
}

void RGASequence::rebuildIndex()
{
    idIndex.clear();
    idIndex.reserve(rgaseq.size());
    for (int i = 0; i < rgaseq.size(); ++i)
        idIndex.insert(encodeId(rgaseq[i].id), i);
}