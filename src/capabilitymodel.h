#pragma once

#include <QAbstractListModel>
#include <QVector>

class CapabilityModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        DescriptionRole,
        CategoryRole,
        EnabledRole
    };
    Q_ENUM(Role)

    explicit CapabilityModel(QString storagePath, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString storagePath() const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setEnabled(int index, bool enabled);
    Q_INVOKABLE bool isEnabled(int index) const;
    Q_INVOKABLE QStringList disabledIds() const;
    Q_INVOKABLE QStringList allIds() const;
    Q_INVOKABLE QStringList categories() const;
    Q_INVOKABLE int categoryCount(const QString &category) const;
    Q_INVOKABLE int enabledCount() const;
    Q_INVOKABLE int totalCount() const;

signals:
    void configurationSaved();
    void errorOccurred(const QString &message);

private:
    struct Capability {
        QString id;
        QString title;
        QString description;
        QString category;
        bool enabled = true;
    };

    QString m_storagePath;
    QVector<Capability> m_capabilities;

    void load();
    void save();
};
