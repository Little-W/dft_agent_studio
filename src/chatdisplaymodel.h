#pragma once

#include <QAbstractListModel>
#include <QVariantList>

// A small batched model for the Chat transcript. QML ListModel emits one
// layout update per insert(), which is expensive when an older history page
// contains hundreds of rows. This model keeps the view's existing delegates,
// batches prepend notifications, and emits dataChanged only for live deltas.
class ChatDisplayModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        EntryRole = Qt::UserRole + 1,
    };

    explicit ChatDisplayModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void sync(const QVariantList &entries);
    Q_INVOKABLE void prepend(const QVariantList &entries, int sourceShift = 0);
    Q_INVOKABLE void replaceAt(int row, const QVariantMap &entry);

signals:
    void countChanged();

private:
    static QVariantMap shiftedEntry(const QVariantMap &entry, int sourceShift);
    static bool sameEntry(const QVariant &left, const QVariant &right);

    QVariantList m_entries;
};
