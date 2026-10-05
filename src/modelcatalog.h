#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QVector>

class ModelCatalog final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString defaultModelId READ defaultModelId NOTIFY catalogChanged)
    Q_PROPERTY(QString activeModelId READ activeModelId NOTIFY activeModelChanged)
    Q_PROPERTY(int activeModelIndex READ activeModelIndex NOTIFY activeModelChanged)
    Q_PROPERTY(int count READ count NOTIFY catalogChanged)

public:
    enum Role {
        ModelIdRole = Qt::UserRole + 1,
        LabelRole,
        RuntimeRole,
        ApiBaseRole,
        ContextWindowRole,
        EnabledRole,
        NotesRole,
        BaseModelPathRole,
        AdapterPathRole,
        InferenceModeRole,
        GpuMemoryGiBRole,
        CpuMemoryGiBRole,
        InputContextTokensRole,
        MaximumNewTokensRole,
        ProviderRole,
        ApiKeyFileRole,
        ApiKeyRole,
        ReasoningEffortRole,
        TemperatureRole,
        TopKRole,
        TopPRole,
        MinPRole,
        RepeatPenaltyRole,
        RepeatLastNRole,
        DryMultiplierRole,
        PresencePenaltyRole,
        FrequencyPenaltyRole,
        EffectiveContextWindowPercentRole,
        AutoCompactTokenLimitRole
    };
    Q_ENUM(Role)

    explicit ModelCatalog(QString storagePath, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString storagePath() const;
    QString defaultModelId() const;
    QString activeModelId() const;
    int activeModelIndex() const;

    Q_INVOKABLE QVariantMap modelAt(int index) const;
    Q_INVOKABLE bool addModel(const QVariantMap &values);
    Q_INVOKABLE bool updateModel(int index, const QVariantMap &values);
    Q_INVOKABLE bool removeModel(int index);
    Q_INVOKABLE bool setActiveModelIndex(int index);
    Q_INVOKABLE void reload();

signals:
    void errorOccurred(const QString &message);
    void catalogChanged();
    void activeModelChanged();

private:
    struct Model {
        QJsonObject preservedFields;
        QString id;
        QString label;
        QString runtime = QStringLiteral("llama_cpp");
        QString apiBase = QStringLiteral("http://127.0.0.1:11503");
        int contextWindow = 65536;
        int effectiveContextWindowPercent = 95;
        int autoCompactTokenLimit = 58'982;
        bool enabled = true;
        QString notes;
        QString baseModelPath;
        QString adapterPath;
        QString llamaServerPath;
        QString inferenceMode = QStringLiteral("gpu");
        double gpuMemoryGiB = 0.0;
        double cpuMemoryGiB = 16.0;
        int inputContextTokens = 16'384;
        int maximumNewTokens = 4096;
        // "legacy" uses the local model worker; "api" uses a direct
        // Responses-compatible cloud/API endpoint.
        QString provider = QStringLiteral("api");
        QString apiKeyFile;
        QString apiKey;
        QString reasoningEffort = QStringLiteral("medium");
        double temperature = 0.6;
        int topK = 40;
        double topP = 0.9;
        double minP = 0.05;
        double repeatPenalty = 1.1;
        int repeatLastN = 256;
        double dryMultiplier = 0.5;
        double presencePenalty = 0.0;
        double frequencyPenalty = 0.0;
        int reconnectMaxAttempts = 10;
        double reconnectDelaySeconds = 1.0;
    };

    QVector<Model> m_models;
    QString m_storagePath;
    QString m_activeModelId;

    bool load();
    bool save();
    bool validate(const Model &model, int ignoredIndex = -1);
    static Model modelFromJson(const QJsonObject &object);
    static QJsonObject modelToJson(const Model &model);
    static QVariantMap toMap(const Model &model);
};
