#include "modelcatalog.h"
#include "studiopaths.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

ModelCatalog::ModelCatalog(QString storagePath, QObject *parent)
    : QAbstractListModel(parent), m_storagePath(std::move(storagePath)) {
    load();
}

int ModelCatalog::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_models.size();
}

int ModelCatalog::count() const { return m_models.size(); }

QVariant ModelCatalog::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_models.size())
        return {};
    const auto &model = m_models.at(index.row());
    switch (role) {
    case ModelIdRole: return model.id;
    case LabelRole: return model.label;
    case RuntimeRole: return model.runtime;
    case ApiBaseRole: return model.apiBase;
    case ContextWindowRole: return model.contextWindow;
    case EnabledRole: return model.enabled;
    case NotesRole: return model.notes;
    case BaseModelPathRole: return model.baseModelPath;
    case AdapterPathRole: return model.adapterPath;
    case InferenceModeRole: return model.inferenceMode;
    case GpuMemoryGiBRole: return model.gpuMemoryGiB;
    case CpuMemoryGiBRole: return model.cpuMemoryGiB;
    case InputContextTokensRole: return model.inputContextTokens;
    case MaximumNewTokensRole: return model.maximumNewTokens;
    case ProviderRole: return model.provider;
    case ApiKeyFileRole: return model.apiKeyFile;
    case ApiKeyRole: return model.apiKey;
    case ReasoningEffortRole: return model.reasoningEffort;
    case TemperatureRole: return model.temperature;
    case TopKRole: return model.topK;
    case TopPRole: return model.topP;
    case MinPRole: return model.minP;
    case RepeatPenaltyRole: return model.repeatPenalty;
    case RepeatLastNRole: return model.repeatLastN;
    case DryMultiplierRole: return model.dryMultiplier;
    case PresencePenaltyRole: return model.presencePenalty;
    case FrequencyPenaltyRole: return model.frequencyPenalty;
    case EffectiveContextWindowPercentRole: return model.effectiveContextWindowPercent;
    case AutoCompactTokenLimitRole: return model.autoCompactTokenLimit;
    default: return {};
    }
}

QHash<int, QByteArray> ModelCatalog::roleNames() const {
    return {
        {ModelIdRole, "modelId"}, {LabelRole, "label"}, {RuntimeRole, "runtime"},
        {ApiBaseRole, "apiBase"}, {ContextWindowRole, "contextWindow"},
        {EnabledRole, "modelEnabled"}, {NotesRole, "notes"},
        {BaseModelPathRole, "baseModelPath"}, {AdapterPathRole, "adapterPath"},
        {InferenceModeRole, "inferenceMode"}, {GpuMemoryGiBRole, "gpuMemoryGiB"},
        {CpuMemoryGiBRole, "cpuMemoryGiB"}, {InputContextTokensRole, "inputContextTokens"},
        {MaximumNewTokensRole, "maximumNewTokens"}, {ProviderRole, "provider"},
        {ApiKeyFileRole, "apiKeyFile"}, {ApiKeyRole, "apiKey"},
        {ReasoningEffortRole, "reasoningEffort"}, {TemperatureRole, "temperature"},
        {TopKRole, "topK"}, {TopPRole, "topP"}, {MinPRole, "minP"},
        {RepeatPenaltyRole, "repeatPenalty"}, {RepeatLastNRole, "repeatLastN"},
        {DryMultiplierRole, "dryMultiplier"}, {PresencePenaltyRole, "presencePenalty"},
        {FrequencyPenaltyRole, "frequencyPenalty"},
        {EffectiveContextWindowPercentRole, "effectiveContextWindowPercent"},
        {AutoCompactTokenLimitRole, "autoCompactTokenLimit"}
    };
}

QString ModelCatalog::storagePath() const { return m_storagePath; }

QString ModelCatalog::defaultModelId() const {
    if (!m_activeModelId.isEmpty()) {
        for (const auto &model : m_models) {
            if (model.id == m_activeModelId && model.enabled)
                return model.id;
        }
    }
    for (const auto &model : m_models) {
        if (model.enabled)
            return model.id;
    }
    return m_models.isEmpty() ? QString() : m_models.constFirst().id;
}

QString ModelCatalog::activeModelId() const { return defaultModelId(); }

int ModelCatalog::activeModelIndex() const {
    const QString selected = m_activeModelId;
    for (int index = 0; index < m_models.size(); ++index) {
        if (m_models.at(index).id == selected && m_models.at(index).enabled)
            return index;
    }
    for (int index = 0; index < m_models.size(); ++index) {
        if (m_models.at(index).enabled)
            return index;
    }
    return m_models.isEmpty() ? -1 : 0;
}

QVariantMap ModelCatalog::modelAt(int index) const {
    if (index < 0 || index >= m_models.size())
        return {};
    return toMap(m_models.at(index));
}

bool ModelCatalog::addModel(const QVariantMap &values) {
    Model model;
    model.id = values.value(QStringLiteral("modelId")).toString().trimmed();
    model.label = values.value(QStringLiteral("label")).toString().trimmed();
    model.runtime = values.value(QStringLiteral("runtime"), QStringLiteral("llama_cpp")).toString().trimmed();
    model.apiBase = values.value(QStringLiteral("apiBase"), QStringLiteral("http://127.0.0.1:11503")).toString().trimmed();
    model.contextWindow = values.value(QStringLiteral("contextWindow"), 65536).toInt();
    model.effectiveContextWindowPercent = values.value(QStringLiteral("effectiveContextWindowPercent"), 95).toInt();
    model.autoCompactTokenLimit = values.value(QStringLiteral("autoCompactTokenLimit"), (model.contextWindow * 9) / 10).toInt();
    model.enabled = values.value(QStringLiteral("enabled"), true).toBool();
    model.notes = values.value(QStringLiteral("notes")).toString().trimmed();
    model.baseModelPath = values.value(QStringLiteral("baseModelPath")).toString().trimmed();
    model.adapterPath = values.value(QStringLiteral("adapterPath")).toString().trimmed();
    model.llamaServerPath = values.value(QStringLiteral("llamaServerPath")).toString().trimmed();
    model.inferenceMode = values.value(QStringLiteral("inferenceMode"), QStringLiteral("gpu")).toString().trimmed();
    model.gpuMemoryGiB = values.value(QStringLiteral("gpuMemoryGiB"), 0.0).toDouble();
    model.cpuMemoryGiB = values.value(QStringLiteral("cpuMemoryGiB"), 16.0).toDouble();
    model.inputContextTokens = values.value(QStringLiteral("inputContextTokens"), 16'384).toInt();
    model.maximumNewTokens = values.value(QStringLiteral("maximumNewTokens"), 4096).toInt();
    if (values.contains(QStringLiteral("provider"))) {
        model.provider = values.value(QStringLiteral("provider")).toString().trimmed().toLower();
    } else {
        model.provider = (model.runtime == QStringLiteral("llama_cpp")
            || model.runtime == QStringLiteral("hf_lora"))
            ? QStringLiteral("legacy") : QStringLiteral("api");
    }
    model.runtime = model.provider == QStringLiteral("legacy")
        ? QStringLiteral("llama_cpp") : QStringLiteral("openai_compatible");
    model.apiKeyFile = values.value(QStringLiteral("apiKeyFile")).toString().trimmed();
    model.apiKey = values.value(QStringLiteral("apiKey")).toString().trimmed();
    model.reasoningEffort = values.value(QStringLiteral("reasoningEffort"), QStringLiteral("medium")).toString().trimmed().toLower();
    model.temperature = values.value(QStringLiteral("temperature"), 0.6).toDouble();
    model.topK = values.value(QStringLiteral("topK"), 40).toInt();
    model.topP = values.value(QStringLiteral("topP"), 0.9).toDouble();
    model.minP = values.value(QStringLiteral("minP"), 0.05).toDouble();
    model.repeatPenalty = values.value(QStringLiteral("repeatPenalty"), 1.1).toDouble();
    model.repeatLastN = values.value(QStringLiteral("repeatLastN"), 256).toInt();
    model.dryMultiplier = values.value(QStringLiteral("dryMultiplier"), 0.5).toDouble();
    model.presencePenalty = values.value(QStringLiteral("presencePenalty"), 0.0).toDouble();
    model.frequencyPenalty = values.value(QStringLiteral("frequencyPenalty"), 0.0).toDouble();
    model.reconnectMaxAttempts = values.value(QStringLiteral("reconnectMaxAttempts"), 10).toInt();
    model.reconnectDelaySeconds = values.value(QStringLiteral("reconnectDelaySeconds"), 1.0).toDouble();
    if (!validate(model))
        return false;
    beginInsertRows({}, m_models.size(), m_models.size());
    m_models.append(model);
    endInsertRows();
    if (m_activeModelId.isEmpty())
        m_activeModelId = model.id;
    return save();
}

bool ModelCatalog::updateModel(int index, const QVariantMap &values) {
    if (index < 0 || index >= m_models.size())
        return false;
    auto model = m_models.at(index);
    const auto update = [&values](QString &field, const char *key) {
        if (values.contains(QString::fromLatin1(key)))
            field = values.value(QString::fromLatin1(key)).toString().trimmed();
    };
    update(model.id, "modelId");
    update(model.label, "label");
    update(model.runtime, "runtime");
    update(model.apiBase, "apiBase");
    update(model.notes, "notes");
    update(model.baseModelPath, "baseModelPath");
    update(model.adapterPath, "adapterPath");
    update(model.llamaServerPath, "llamaServerPath");
    update(model.inferenceMode, "inferenceMode");
    if (values.contains(QStringLiteral("contextWindow"))) {
        model.contextWindow = values.value(QStringLiteral("contextWindow")).toInt();
        if (!values.contains(QStringLiteral("autoCompactTokenLimit")))
            model.autoCompactTokenLimit = qMin(model.autoCompactTokenLimit, (model.contextWindow * 9) / 10);
    }
    if (values.contains(QStringLiteral("effectiveContextWindowPercent")))
        model.effectiveContextWindowPercent = values.value(QStringLiteral("effectiveContextWindowPercent")).toInt();
    if (values.contains(QStringLiteral("autoCompactTokenLimit")))
        model.autoCompactTokenLimit = values.value(QStringLiteral("autoCompactTokenLimit")).toInt();
    if (values.contains(QStringLiteral("enabled")))
        model.enabled = values.value(QStringLiteral("enabled")).toBool();
    if (values.contains(QStringLiteral("gpuMemoryGiB")))
        model.gpuMemoryGiB = values.value(QStringLiteral("gpuMemoryGiB")).toDouble();
    if (values.contains(QStringLiteral("cpuMemoryGiB")))
        model.cpuMemoryGiB = values.value(QStringLiteral("cpuMemoryGiB")).toDouble();
    if (values.contains(QStringLiteral("inputContextTokens")))
        model.inputContextTokens = values.value(QStringLiteral("inputContextTokens")).toInt();
    if (values.contains(QStringLiteral("maximumNewTokens")))
        model.maximumNewTokens = values.value(QStringLiteral("maximumNewTokens")).toInt();
    if (values.contains(QStringLiteral("provider")))
        model.provider = values.value(QStringLiteral("provider")).toString().trimmed().toLower();
    if (model.provider == QStringLiteral("codex"))
        model.provider = QStringLiteral("api");
    model.runtime = model.provider == QStringLiteral("legacy")
        ? QStringLiteral("llama_cpp") : QStringLiteral("openai_compatible");
    if (values.contains(QStringLiteral("apiKeyFile")))
        model.apiKeyFile = values.value(QStringLiteral("apiKeyFile")).toString().trimmed();
    if (values.contains(QStringLiteral("apiKey")))
        model.apiKey = values.value(QStringLiteral("apiKey")).toString().trimmed();
    if (values.contains(QStringLiteral("reasoningEffort")))
        model.reasoningEffort = values.value(QStringLiteral("reasoningEffort")).toString().trimmed().toLower();
    if (values.contains(QStringLiteral("temperature")))
        model.temperature = values.value(QStringLiteral("temperature")).toDouble();
    if (values.contains(QStringLiteral("topK")))
        model.topK = values.value(QStringLiteral("topK")).toInt();
    if (values.contains(QStringLiteral("topP")))
        model.topP = values.value(QStringLiteral("topP")).toDouble();
    if (values.contains(QStringLiteral("minP")))
        model.minP = values.value(QStringLiteral("minP")).toDouble();
    if (values.contains(QStringLiteral("repeatPenalty")))
        model.repeatPenalty = values.value(QStringLiteral("repeatPenalty")).toDouble();
    if (values.contains(QStringLiteral("repeatLastN")))
        model.repeatLastN = values.value(QStringLiteral("repeatLastN")).toInt();
    if (values.contains(QStringLiteral("dryMultiplier")))
        model.dryMultiplier = values.value(QStringLiteral("dryMultiplier")).toDouble();
    if (values.contains(QStringLiteral("presencePenalty")))
        model.presencePenalty = values.value(QStringLiteral("presencePenalty")).toDouble();
    if (values.contains(QStringLiteral("frequencyPenalty")))
        model.frequencyPenalty = values.value(QStringLiteral("frequencyPenalty")).toDouble();
    if (values.contains(QStringLiteral("reconnectMaxAttempts")))
        model.reconnectMaxAttempts = values.value(QStringLiteral("reconnectMaxAttempts")).toInt();
    if (values.contains(QStringLiteral("reconnectDelaySeconds")))
        model.reconnectDelaySeconds = values.value(QStringLiteral("reconnectDelaySeconds")).toDouble();
    if (!validate(model, index))
        return false;
    const bool changingActive = m_models.at(index).id == m_activeModelId;
    m_models[index] = model;
    if (changingActive && !model.enabled) {
        m_activeModelId.clear();
        for (const auto &candidate : m_models) {
            if (candidate.enabled) {
                m_activeModelId = candidate.id;
                break;
            }
        }
    }
    emit dataChanged(this->index(index), this->index(index));
    return save();
}

bool ModelCatalog::setActiveModelIndex(int index) {
    if (index < 0 || index >= m_models.size() || !m_models.at(index).enabled)
        return false;
    const QString id = m_models.at(index).id;
    if (id == m_activeModelId)
        return true;
    m_activeModelId = id;
    return save();
}

bool ModelCatalog::removeModel(int index) {
    if (index < 0 || index >= m_models.size())
        return false;
    if (m_models.size() == 1) {
        emit errorOccurred(tr("至少保留一个模型配置。"));
        return false;
    }
    const bool removingActive = m_models.at(index).id == m_activeModelId;
    beginRemoveRows({}, index, index);
    m_models.removeAt(index);
    endRemoveRows();
    if (removingActive)
        m_activeModelId = m_models.isEmpty() ? QString() : m_models.constFirst().id;
    return save();
}

void ModelCatalog::reload() {
    beginResetModel();
    load();
    endResetModel();
    emit catalogChanged();
}

bool ModelCatalog::load() {
    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("无法读取模型配置：%1").arg(m_storagePath));
        return false;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        emit errorOccurred(tr("模型配置 JSON 格式无效。"));
        return false;
    }
    m_models.clear();
    for (const auto value : document.object().value("models").toArray()) {
        if (value.isObject())
            m_models.append(modelFromJson(value.toObject()));
    }
    if (m_models.isEmpty())
        emit errorOccurred(tr("模型配置不能为空。"));
    m_activeModelId = document.object().value(QStringLiteral("active_model_id")).toString().trimmed();
    bool activeFound = false;
    for (const auto &model : m_models) {
        if (model.id == m_activeModelId && model.enabled) {
            activeFound = true;
            break;
        }
    }
    if (!activeFound)
        m_activeModelId = m_models.isEmpty() ? QString() : m_models.constFirst().id;
    return !m_models.isEmpty();
}

bool ModelCatalog::save() {
    QFileInfo info(m_storagePath);
    QDir().mkpath(info.absolutePath());
    QJsonArray models;
    for (const auto &model : m_models)
        models.append(modelToJson(model));
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("无法写入模型配置：%1").arg(m_storagePath));
        return false;
    }
    file.write(QJsonDocument(QJsonObject{
        {"version", 1}, {"active_model_id", m_activeModelId}, {"models", models}
    }).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        emit errorOccurred(tr("保存模型配置失败：%1").arg(m_storagePath));
        return false;
    }
    emit catalogChanged();
    emit activeModelChanged();
    return true;
}

bool ModelCatalog::validate(const Model &model, int ignoredIndex) {
    // Model IDs are opaque provider identifiers.  In particular, local/API
    // configurations commonly use an absolute model path ("/home/..."),
    // while hosted providers may use characters outside the old filename
    // regex.  Only reject control characters and unbounded input here.
    const bool hasControlCharacter = std::any_of(model.id.cbegin(), model.id.cend(), [](QChar character) {
        const ushort code = character.unicode();
        return code < 0x20 || code == 0x7f;
    });
    if (model.id.isEmpty() || model.id.size() > 512 || hasControlCharacter) {
        emit errorOccurred(tr("模型 ID 不能为空、不能包含控制字符，且长度不能超过 512 个字符。"));
        return false;
    }
    if (model.label.isEmpty() || model.runtime.isEmpty()) {
        emit errorOccurred(tr("模型名称和运行时不能为空。"));
        return false;
    }
    if (!model.apiBase.isEmpty() && !model.apiBase.startsWith(QStringLiteral("http://")) && !model.apiBase.startsWith(QStringLiteral("https://"))) {
        emit errorOccurred(tr("模型 API 地址必须以 http:// 或 https:// 开头。"));
        return false;
    }
    if (model.contextWindow < 2048 || model.contextWindow > 262144) {
        emit errorOccurred(tr("上下文窗口必须在 2048 到 262144 之间。"));
        return false;
    }
    if (model.effectiveContextWindowPercent < 1 || model.effectiveContextWindowPercent > 100) {
        emit errorOccurred(tr("有效上下文比例必须在 1 到 100 之间。"));
        return false;
    }
    if (model.autoCompactTokenLimit < 1'024 || model.autoCompactTokenLimit > (model.contextWindow * 9) / 10) {
        emit errorOccurred(tr("自动压缩阈值必须在 1024 到模型上下文窗口的 90% 之间。"));
        return false;
    }
    if (model.provider != QStringLiteral("legacy") && model.provider != QStringLiteral("api")) {
        emit errorOccurred(tr("推理提供者必须是 legacy（本地）或 api（云端/API）。"));
        return false;
    }
    if (model.reconnectMaxAttempts < 1 || model.reconnectMaxAttempts > 20) {
        emit errorOccurred(tr("断联重试次数必须在 1 到 20 次之间。"));
        return false;
    }
    if (model.reconnectDelaySeconds < 0.1 || model.reconnectDelaySeconds > 60.0) {
        emit errorOccurred(tr("断联重试间隔必须在 0.1 到 60 秒之间。"));
        return false;
    }
    if (model.temperature < 0.0 || model.temperature > 2.0) {
        emit errorOccurred(tr("temperature 必须在 0 到 2 之间。"));
        return false;
    }
    if (model.topK < 0 || model.topK > 100'000) {
        emit errorOccurred(tr("top_k 必须在 0 到 100000 之间。"));
        return false;
    }
    if (model.topP < 0.0 || model.topP > 1.0 || model.minP < 0.0 || model.minP > 1.0) {
        emit errorOccurred(tr("top_p 和 min_p 必须在 0 到 1 之间。"));
        return false;
    }
    if (model.repeatPenalty < 1.0 || model.repeatPenalty > 2.0) {
        emit errorOccurred(tr("重复惩罚必须在 1 到 2 之间。"));
        return false;
    }
    if (model.repeatLastN < 0 || model.repeatLastN > 4096) {
        emit errorOccurred(tr("重复窗口必须在 0 到 4096 之间。"));
        return false;
    }
    if (model.dryMultiplier < 0.0 || model.dryMultiplier > 5.0) {
        emit errorOccurred(tr("DRY 防复读强度必须在 0 到 5 之间。"));
        return false;
    }
    if (model.presencePenalty < -2.0 || model.presencePenalty > 2.0
        || model.frequencyPenalty < -2.0 || model.frequencyPenalty > 2.0) {
        emit errorOccurred(tr("presence/frequency penalty 必须在 -2 到 2 之间。"));
        return false;
    }
    if (model.inputContextTokens < 1'024 || model.inputContextTokens >= model.contextWindow) {
        emit errorOccurred(tr("历史记录容量必须在 1024 到 %1 之间。").arg(model.contextWindow - 1));
        return false;
    }
    if (model.provider == QStringLiteral("api")) {
        if (model.apiBase.isEmpty()) {
            emit errorOccurred(tr("云端模型需要设置 API 地址。"));
            return false;
        }
        if (model.reasoningEffort != QStringLiteral("low")
            && model.reasoningEffort != QStringLiteral("medium")
            && model.reasoningEffort != QStringLiteral("high")
            && model.reasoningEffort != QStringLiteral("xhigh")
            && model.reasoningEffort != QStringLiteral("max")
            && model.reasoningEffort != QStringLiteral("ultra")) {
            emit errorOccurred(tr("云端模型思考深度必须是 low、medium、high、xhigh、max 或 ultra。"));
            return false;
        }
        if (model.maximumNewTokens < 1 || model.maximumNewTokens > model.contextWindow) {
            emit errorOccurred(tr("云端输出上限必须在 1 到上下文窗口之间。"));
            return false;
        }
    } else if (model.provider == QStringLiteral("legacy")
        && model.runtime == QStringLiteral("llama_cpp")) {
        if (model.baseModelPath.isEmpty()) {
            emit errorOccurred(tr("本地模型缺少所需的模型文件路径。"));
            return false;
        }
        if (model.inferenceMode != QStringLiteral("gpu")
            && model.inferenceMode != QStringLiteral("cpu_gpu")
            && model.inferenceMode != QStringLiteral("cpu")) {
            emit errorOccurred(tr("推理模式必须是仅 GPU、CPU+GPU 或仅 CPU。"));
            return false;
        }
        if (model.inferenceMode == QStringLiteral("cpu_gpu") && model.gpuMemoryGiB <= 0.0) {
            emit errorOccurred(tr("GPU+CPU 自动分层模式需要设置有效的显存预算。"));
            return false;
        }
        const int maximumOutput = model.runtime == QStringLiteral("llama_cpp")
            ? qMax(1, model.contextWindow - model.inputContextTokens)
            : 16'384;
        if (model.maximumNewTokens < 1 || model.maximumNewTokens > maximumOutput) {
            emit errorOccurred(tr("输出上限必须在 1 到 %1 之间。").arg(maximumOutput));
            return false;
        }
    } else if (model.provider == QStringLiteral("legacy")) {
        emit errorOccurred(tr("本地模型仅支持 llama_cpp。"));
        return false;
    }
    for (int index = 0; index < m_models.size(); ++index) {
        if (index != ignoredIndex && m_models.at(index).id == model.id) {
            emit errorOccurred(tr("模型 ID 已存在：%1").arg(model.id));
            return false;
        }
    }
    return true;
}

ModelCatalog::Model ModelCatalog::modelFromJson(const QJsonObject &object) {
    Model model;
    model.preservedFields = object;
    model.id = object.value("id").toString();
    model.label = object.value("label").toString();
    model.runtime = object.value("runtime").toString("llama_cpp");
    model.apiBase = object.value("api_base").toString("http://127.0.0.1:11503");
    model.contextWindow = object.value("context_window").toInt(65536);
    model.effectiveContextWindowPercent = object.value("effective_context_window_percent").toInt(95);
    model.autoCompactTokenLimit = object.value("auto_compact_token_limit").toInt((model.contextWindow * 9) / 10);
    model.enabled = object.value("enabled").toBool(true);
    model.notes = object.value("notes").toString();
    model.baseModelPath = object.value("base_model_path").toString();
    model.adapterPath = object.value("adapter_path").toString();
    model.llamaServerPath = object.value("llama_server_path").toString();
    model.inferenceMode = object.value("inference_mode").toString("gpu");
    model.gpuMemoryGiB = object.value("gpu_memory_gib").toDouble(0.0);
    model.cpuMemoryGiB = object.value("cpu_memory_gib").toDouble(16.0);
    model.inputContextTokens = object.value("input_context_tokens").toInt(
        object.value("history_context_tokens").toInt(16'384)
    );
    model.maximumNewTokens = object.value("maximum_new_tokens").toInt(4096);
    if (object.contains(QStringLiteral("provider"))) {
        model.provider = object.value("provider").toString("api").trimmed().toLower();
        if (model.provider == QStringLiteral("codex"))
            model.provider = QStringLiteral("api");
    } else {
        // Older catalogs described only the runtime; infer local inference for
        // llama.cpp/HF entries so adding cloud support does not relabel them.
        model.provider = (model.runtime == QStringLiteral("llama_cpp")
            || model.runtime == QStringLiteral("hf_lora"))
            ? QStringLiteral("legacy") : QStringLiteral("api");
    }
    model.runtime = model.provider == QStringLiteral("legacy")
        ? QStringLiteral("llama_cpp") : QStringLiteral("openai_compatible");
    model.apiKeyFile = resolveStudioRecordPath(object.value("api_key_file").toString());
    model.apiKey = object.value("api_key").toString();
    model.reasoningEffort = object.value("reasoning_effort").toString("medium").trimmed().toLower();
    model.temperature = object.value("temperature").toDouble(0.6);
    model.topK = object.value("top_k").toInt(40);
    model.topP = object.value("top_p").toDouble(0.9);
    model.minP = object.value("min_p").toDouble(0.05);
    model.repeatPenalty = object.value("repeat_penalty").toDouble(1.1);
    model.repeatLastN = object.value("repeat_last_n").toInt(256);
    model.dryMultiplier = object.value("dry_multiplier").toDouble(0.5);
    model.presencePenalty = object.value("presence_penalty").toDouble(0.0);
    model.frequencyPenalty = object.value("frequency_penalty").toDouble(0.0);
    model.reconnectMaxAttempts = object.value("reconnect_max_attempts").toInt(10);
    model.reconnectDelaySeconds = object.value("reconnect_delay_seconds").toDouble(1.0);
    return model;
}

QJsonObject ModelCatalog::modelToJson(const Model &model) {
    QJsonObject object = model.preservedFields;
    object.insert("id", model.id);
    object.insert("label", model.label);
    object.insert("runtime", model.runtime);
    object.insert("api_base", model.apiBase);
    object.insert("context_window", model.contextWindow);
    object.insert("effective_context_window_percent", model.effectiveContextWindowPercent);
    object.insert("auto_compact_token_limit", model.autoCompactTokenLimit);
    object.insert("enabled", model.enabled);
    object.insert("notes", model.notes);
    object.insert("base_model_path", model.baseModelPath);
    object.insert("adapter_path", model.adapterPath);
    object.insert("llama_server_path", model.llamaServerPath);
    object.insert("inference_mode", model.inferenceMode);
    object.insert("gpu_memory_gib", model.gpuMemoryGiB);
    object.insert("cpu_memory_gib", model.cpuMemoryGiB);
    object.insert("input_context_tokens", model.inputContextTokens);
    object.insert("maximum_new_tokens", model.maximumNewTokens);
    object.insert("provider", model.provider);
    object.insert("api_key_file", model.apiKeyFile);
    object.remove("api_key");
    if (!model.apiKey.isEmpty())
        object.insert("api_key", model.apiKey);
    object.insert("reasoning_effort", model.reasoningEffort);
    object.insert("temperature", model.temperature);
    object.insert("top_k", model.topK);
    object.insert("top_p", model.topP);
    object.insert("min_p", model.minP);
    object.insert("repeat_penalty", model.repeatPenalty);
    object.insert("repeat_last_n", model.repeatLastN);
    object.insert("dry_multiplier", model.dryMultiplier);
    object.insert("presence_penalty", model.presencePenalty);
    object.insert("frequency_penalty", model.frequencyPenalty);
    object.insert("reconnect_max_attempts", model.reconnectMaxAttempts);
    object.insert("reconnect_delay_seconds", model.reconnectDelaySeconds);
    return object;
}

QVariantMap ModelCatalog::toMap(const Model &model) {
    return {
        {"modelId", model.id}, {"label", model.label}, {"runtime", model.runtime}, {"apiBase", model.apiBase},
        {"contextWindow", model.contextWindow},
        {"effectiveContextWindowPercent", model.effectiveContextWindowPercent},
        {"autoCompactTokenLimit", model.autoCompactTokenLimit},
        {"enabled", model.enabled}, {"notes", model.notes},
        {"baseModelPath", model.baseModelPath}, {"adapterPath", model.adapterPath},
        {"llamaServerPath", model.llamaServerPath},
        {"inferenceMode", model.inferenceMode}, {"gpuMemoryGiB", model.gpuMemoryGiB},
        {"cpuMemoryGiB", model.cpuMemoryGiB}, {"inputContextTokens", model.inputContextTokens},
        {"maximumNewTokens", model.maximumNewTokens}, {"provider", model.provider},
        {"apiKeyFile", model.apiKeyFile},
        {"apiKey", model.apiKey},
        {"reasoningEffort", model.reasoningEffort},
        {"temperature", model.temperature}, {"topK", model.topK},
        {"topP", model.topP}, {"minP", model.minP},
        {"repeatPenalty", model.repeatPenalty}, {"repeatLastN", model.repeatLastN},
        {"dryMultiplier", model.dryMultiplier},
        {"presencePenalty", model.presencePenalty},
        {"frequencyPenalty", model.frequencyPenalty},
        {"reconnectMaxAttempts", model.reconnectMaxAttempts},
        {"reconnectDelaySeconds", model.reconnectDelaySeconds}
    };
}
