#include "capabilitymodel.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

CapabilityModel::CapabilityModel(QString storagePath, QObject *parent)
    : QAbstractListModel(parent), m_storagePath(std::move(storagePath)) {
    m_capabilities = {
        {"project_dft_execution_readiness", "Project readiness", "Read-only check of configured RTL, SDC, libraries, and dc_shell.", "Skills", true},
        {"run_and_verify_project_dft_flow", "Project DFT flow", "Run the configured scan DFT flow in isolation and verify it twice.", "Skills", true},
        {"run_and_verify_external_dft_flow", "External RTL flow", "Run a generated DFT flow, diagnose fresh reports, and repair RTL only when evidence identifies a design defect.", "Skills", true},
        {"inspect_benchmark", "Inspect benchmark", "Read metadata for a FAN_ATPG allow-listed benchmark circuit.", "Agent tools", true},
        {"optimize_atpg_goal", "Optimize ATPG", "Select an approved profile that meets the coverage target.", "Agent tools", true},
        {"project_registry", "Project registry", "Read and update project goals and execution directories.", "Agent tools", true},
        {"evidence_validator", "Evidence validator", "Cross-check execution evidence, reports, and counter-evidence.", "Agent tools", true},
        {"isolated_terminal_diagnostics", "Isolated terminal diagnosis", "Inspect fresh controlled-flow logs and reports, then trace confirmed failures into project configuration or RTL.", "Skills", true},
        {"agent_shell_and_files", "Agent shell and files", "Inspect projects with isolated shell, file reading, and search without loading whole large files into context.", "Skills", true},
        {"studio_configuration", "DFT Studio configuration", "Read and edit DFT Agent Studio project, model, and capability settings.", "Skills", true}
    };
    load();
}

int CapabilityModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_capabilities.size(); }

QVariant CapabilityModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_capabilities.size())
        return {};
    const auto &item = m_capabilities.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case TitleRole: return item.title;
    case DescriptionRole: return item.description;
    case CategoryRole: return item.category;
    case EnabledRole: return item.enabled;
    default: return {};
    }
}

QHash<int, QByteArray> CapabilityModel::roleNames() const {
    return {{IdRole, "capabilityId"}, {TitleRole, "title"}, {DescriptionRole, "description"}, {CategoryRole, "category"}, {EnabledRole, "enabled"}};
}

QString CapabilityModel::storagePath() const { return m_storagePath; }

void CapabilityModel::reload() {
    beginResetModel();
    load();
    endResetModel();
}

void CapabilityModel::setEnabled(int index, bool enabled) {
    if (index < 0 || index >= m_capabilities.size() || m_capabilities[index].enabled == enabled)
        return;
    m_capabilities[index].enabled = enabled;
    emit dataChanged(this->index(index), this->index(index), {EnabledRole});
    save();
}

bool CapabilityModel::isEnabled(int index) const {
    return index >= 0 && index < m_capabilities.size() && m_capabilities.at(index).enabled;
}

QStringList CapabilityModel::disabledIds() const {
    QStringList disabled;
    for (const auto &item : m_capabilities) {
        if (!item.enabled)
            disabled.append(item.id);
    }
    return disabled;
}

QStringList CapabilityModel::allIds() const {
    QStringList ids;
    for (const auto &item : m_capabilities)
        ids.append(item.id);
    return ids;
}

QStringList CapabilityModel::categories() const { return {"Skills", "Agent tools"}; }

int CapabilityModel::categoryCount(const QString &category) const {
    int count = 0;
    for (const auto &item : m_capabilities)
        count += item.category == category ? 1 : 0;
    return count;
}

int CapabilityModel::enabledCount() const {
    int count = 0;
    for (const auto &item : m_capabilities)
        count += item.enabled ? 1 : 0;
    return count;
}

int CapabilityModel::totalCount() const { return m_capabilities.size(); }

void CapabilityModel::load() {
    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;
    const auto values = document.object().value("disabled").toArray();
    QSet<QString> disabled;
    for (const auto &value : values)
        disabled.insert(value.toString());
    for (auto &item : m_capabilities)
        item.enabled = !disabled.contains(item.id);
}

void CapabilityModel::save() {
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("无法保存 capability 配置。"));
        return;
    }
    QJsonArray disabled;
    for (const auto &item : m_capabilities) {
        if (!item.enabled)
            disabled.append(item.id);
    }
    file.write(QJsonDocument(QJsonObject{{"disabled", disabled}}).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        emit errorOccurred(tr("保存 capability 配置失败。"));
        return;
    }
    emit configurationSaved();
}
