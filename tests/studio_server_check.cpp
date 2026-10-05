#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cstdio>

#ifndef DFT_STUDIO_SERVER_PATH
#define DFT_STUDIO_SERVER_PATH ""
#endif

namespace {
bool check(bool condition, const char *message) {
    if (condition)
        return true;
    std::fprintf(stderr, "Check failed: %s\n", message);
    return false;
}

QJsonObject request(int id, const QString &method, const QJsonObject &params = {}) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id}, {QStringLiteral("method"), method},
            {QStringLiteral("params"), params}};
}

bool writeJsonLine(QProcess &process, const QJsonObject &object) {
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    return process.write(line) == line.size() && process.waitForBytesWritten(2'000);
}

bool readJsonLine(QProcess &process, QJsonObject *output) {
    while (!process.canReadLine()) {
        if (!process.waitForReadyRead(5'000))
            return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(process.readLine().trimmed());
    if (!document.isObject())
        return false;
    *output = document.object();
    return true;
}
}

int main() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "temporary Studio root is created"))
        return 1;
    const QString root = temporary.path();
    const QString projectRoot = QDir(root).filePath(QStringLiteral("project"));
    const QString configRoot = QDir(root).filePath(QStringLiteral(".dft_agent_studio/config"));
    if (!QDir().mkpath(projectRoot) || !QDir().mkpath(configRoot))
        return 1;
    QFile projects(QDir(configRoot).filePath(QStringLiteral("projects.json")));
    if (!projects.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 1;
    projects.write(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("projects"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("fixture")},
            {QStringLiteral("name"), QStringLiteral("Server fixture")},
            {QStringLiteral("kind"), QStringLiteral("rtl")},
            {QStringLiteral("root"), projectRoot},
        }}}
    }).toJson(QJsonDocument::Compact));
    projects.close();

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HOME"), root);
    process.setProcessEnvironment(environment);
    process.start(QString::fromLocal8Bit(DFT_STUDIO_SERVER_PATH),
                  {QStringLiteral("--agent-root"), root});
    if (!check(process.waitForStarted(5'000), "native server starts"))
        return 1;

    QJsonObject response;
    if (!check(writeJsonLine(process, request(1, QStringLiteral("initialize")))
                   && readJsonLine(process, &response), "initialize request receives a response"))
        return 1;
    const QJsonObject initialization = response.value(QStringLiteral("result")).toObject();
    if (!check(response.value(QStringLiteral("id")).toInt() == 1
                   && initialization.value(QStringLiteral("protocolVersion")).toString() == QStringLiteral("2.0")
                   && initialization.value(QStringLiteral("capabilities")).toObject()
                       .value(QStringLiteral("live_steering")).toBool(),
               "initialize advertises the native protocol and live controls"))
        return 1;

    if (!check(writeJsonLine(process, request(2, QStringLiteral("project/list")))
                   && readJsonLine(process, &response), "project/list request receives a response"))
        return 1;
    const QJsonObject projectResult = response.value(QStringLiteral("result")).toObject()
        .value(QStringLiteral("result")).toObject();
    const QJsonArray projectsResult = projectResult.value(QStringLiteral("projects")).toArray();
    if (!check(response.value(QStringLiteral("id")).toInt() == 2 && projectsResult.size() == 1
                   && projectsResult.first().toObject().value(QStringLiteral("id")).toString()
                       == QStringLiteral("fixture"),
               "native server routes management requests to StudioNativeApi"))
        return 1;

    if (!check(writeJsonLine(process, request(3, QStringLiteral("does/not/exist")))
                   && readJsonLine(process, &response), "unknown method receives a JSON-RPC error"))
        return 1;
    if (!check(response.value(QStringLiteral("error")).toObject()
                   .value(QStringLiteral("code")).toInt() == -32601,
               "unknown methods use method-not-found error semantics"))
        return 1;

    process.terminate();
    if (!process.waitForFinished(2'000)) {
        process.kill();
        process.waitForFinished(2'000);
    }
    return 0;
}
