#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

class ProjectModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        RootRole,
        RtlRootRole,
        TopRole,
        TopModuleRole,
        GoalRole,
        NotesRole,
        ManagedRole,
        ModelRole,
        FlowProfileRole,
        MinimumCoverageRole,
        MaximumDftDrcViolationsRole,
        LibraryDirRole,
        LibraryFileRole,
        LibraryProfileRole,
        StatusRole
    };
    Q_ENUM(Role)

    explicit ProjectModel(QString storagePath, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;
    void setCurrentIndex(int index);
    QString storagePath() const;

    Q_INVOKABLE QVariantMap currentProject() const;
    Q_INVOKABLE QVariantMap projectAt(int index) const;
    Q_INVOKABLE int indexOfProjectId(const QString &projectId) const;
    Q_INVOKABLE bool addProject(const QString &folderPath);
    Q_INVOKABLE bool addProject(const QString &folderPath, const QString &kind);
    Q_INVOKABLE bool removeProject(int index);
    Q_INVOKABLE bool updateProject(int index, const QVariantMap &values);
    Q_INVOKABLE bool setProjectGoal(int index, const QString &goal);
    Q_INVOKABLE void reload();

signals:
    void currentIndexChanged();
    void errorOccurred(const QString &message);
    void projectSaved();

private:
    struct Project {
        QString id;
        QString name;
        QString kind;
        QString root;
        QString rtlRoot;
        QString top;
        QString goal;
        QString notes;
        QStringList relatedDocuments;
        bool managed = false;
        QString modelName;
        QString flowProfile;
        double minimumCoverage = -1.0;
        int maximumDftDrcViolations = -1;
        QString libraryDir;
        QString libraryFile;
        QString libraryProfile;
        QJsonObject dftExecution;
        QJsonObject flowModules;
    };

    QVector<Project> m_projects;
    QString m_storagePath;
    int m_currentIndex = 0;

    bool load();
    bool save();
    static Project projectFromJson(const QJsonObject &object);
    static QJsonObject projectToJson(const Project &project);
    static QString generatedId(const QString &name);
    static QString projectStatus(const Project &project);
    QVariantMap toMap(const Project &project) const;
};
