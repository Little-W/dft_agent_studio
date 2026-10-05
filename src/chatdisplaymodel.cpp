#include "chatdisplaymodel.h"

ChatDisplayModel::ChatDisplayModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ChatDisplayModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant ChatDisplayModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    if (role == EntryRole)
        return m_entries.at(index.row());
    return {};
}

QHash<int, QByteArray> ChatDisplayModel::roleNames() const {
    return {{EntryRole, QByteArrayLiteral("entry")}};
}

bool ChatDisplayModel::sameEntry(const QVariant &left, const QVariant &right) {
    return left == right;
}

QVariantMap ChatDisplayModel::shiftedEntry(const QVariantMap &entry, int sourceShift) {
    if (sourceShift <= 0)
        return entry;
    QVariantMap shifted = entry;
    shifted.insert(QStringLiteral("sourceIndex"),
                   entry.value(QStringLiteral("sourceIndex")).toInt() + sourceShift);
    const QString kind = entry.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("tool_group") && kind != QStringLiteral("activity_group"))
        return shifted;

    shifted.insert(QStringLiteral("groupId"),
                   (kind == QStringLiteral("activity_group")
                        ? QStringLiteral("activity-group-")
                        : QStringLiteral("tool-group-"))
                       + shifted.value(QStringLiteral("sourceIndex")).toString());
    QVariantList children;
    const QVariantList originalChildren = entry.value(QStringLiteral("children")).toList();
    children.reserve(originalChildren.size());
    for (const QVariant &value : originalChildren) {
        QVariantMap child = value.toMap();
        child.insert(QStringLiteral("sourceIndex"),
                     child.value(QStringLiteral("sourceIndex")).toInt() + sourceShift);
        children.append(child);
    }
    shifted.insert(QStringLiteral("children"), children);
    return shifted;
}

void ChatDisplayModel::sync(const QVariantList &entries) {
    const int oldCount = m_entries.size();
    const int newCount = entries.size();
    if (oldCount > newCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        m_entries.erase(m_entries.begin() + newCount, m_entries.end());
        endRemoveRows();
    }

    const int commonCount = qMin(m_entries.size(), newCount);
    for (int row = 0; row < commonCount; ++row) {
        const QVariant &next = entries.at(row);
        if (sameEntry(m_entries.at(row), next))
            continue;
        m_entries[row] = next;
        emit dataChanged(index(row), index(row), {EntryRole});
    }

    if (newCount > m_entries.size()) {
        const int first = m_entries.size();
        beginInsertRows({}, first, newCount - 1);
        for (int row = first; row < newCount; ++row)
            m_entries.append(entries.at(row));
        endInsertRows();
    }
    if (oldCount != m_entries.size())
        emit countChanged();
}

void ChatDisplayModel::prepend(const QVariantList &entries, int sourceShift) {
    if (entries.isEmpty() && sourceShift <= 0)
        return;

    const int oldCount = m_entries.size();
    if (sourceShift > 0 && oldCount > 0) {
        QVariantList shifted;
        shifted.reserve(oldCount);
        for (const QVariant &value : m_entries)
            shifted.append(shiftedEntry(value.toMap(), sourceShift));
        m_entries = shifted;
        emit dataChanged(index(0), index(oldCount - 1), {EntryRole});
    }

    if (entries.isEmpty())
        return;
    beginInsertRows({}, 0, entries.size() - 1);
    QVariantList combined = entries;
    combined += m_entries;
    m_entries = combined;
    endInsertRows();
    emit countChanged();
}

void ChatDisplayModel::replaceAt(int row, const QVariantMap &entry) {
    if (row < 0 || row >= m_entries.size())
        return;
    const QVariant next(entry);
    if (sameEntry(m_entries.at(row), next))
        return;
    m_entries[row] = next;
    emit dataChanged(index(row), index(row), {EntryRole});
}
