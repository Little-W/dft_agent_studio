#include "../src/localmodelruntimeservice.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFile>

#include <cstdio>

namespace {
bool require(bool value, const char *message) {
    if (!value)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

QString optionValue(const QStringList &args, const QString &option) {
    const int index = args.indexOf(option);
    return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString{};
}
}

int main() {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    const QString root = temporary.path();
    const QString binary = QDir(root).filePath(QStringLiteral("llama-server"));
    const QString modelPath = QDir(root).filePath(QStringLiteral("model.gguf"));
    const QString adapterPath = QDir(root).filePath(QStringLiteral("adapter.gguf"));
    const QString customBinary = QDir(root).filePath(QStringLiteral("custom/llama-server"));
    QDir().mkpath(QFileInfo(customBinary).absolutePath());
    for (const QString &path : {binary, modelPath, adapterPath, customBinary}) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture") < 0)
            return 2;
    }

    QString error;
    const QString selectedBinary = LocalModelRuntimeService::findLlamaServerBinary(
        root, QStringLiteral("custom/llama-server"), &error);
    if (!require(selectedBinary == customBinary, "configured llama-server path resolves relative to the project root"))
        return 1;
    if (!require(LocalModelRuntimeService::findLlamaServerBinary(
                     root, QStringLiteral("missing/llama-server"), &error).isEmpty()
                 && error.contains(QStringLiteral("does not exist")),
                 "invalid configured llama-server path reports a direct error"))
        return 1;

    QVariantMap model{{QStringLiteral("modelId"), QStringLiteral("local-test")},
                      {QStringLiteral("apiBase"), QStringLiteral("http://127.0.0.1:11503/v1")},
                      {QStringLiteral("baseModelPath"), QStringLiteral("model.gguf")},
                      {QStringLiteral("adapterPath"), QStringLiteral("adapter.gguf")},
                      {QStringLiteral("contextWindow"), 32'768},
                      {QStringLiteral("maximumNewTokens"), 2'048},
                      {QStringLiteral("inferenceMode"), QStringLiteral("cpu_gpu")},
                      {QStringLiteral("gpuMemoryGiB"), 8.0}};
    QStringList command;
    bool ok = LocalModelRuntimeService::llamaServerCommand(model, root, binary, 24'576, &command, &error);
    ok &= require(ok, "configured GGUF model and adapter produce a server command");
    ok &= require(command.first() == binary, "configured llama-server binary is first");
    ok &= require(optionValue(command, QStringLiteral("--model")) == modelPath,
                  "relative model path resolves against the project root");
    ok &= require(optionValue(command, QStringLiteral("--lora")) == adapterPath,
                  "GGUF adapter is passed through");
    ok &= require(optionValue(command, QStringLiteral("--ctx-size")) == QStringLiteral("32768"),
                  "context window is preserved");
    ok &= require(optionValue(command, QStringLiteral("--n-predict")) == QStringLiteral("2048"),
                  "maximum output is preserved");
    ok &= require(optionValue(command, QStringLiteral("--fit-target")) == QStringLiteral("16384"),
                  "GPU budget is translated to the required free-memory margin");
    ok &= require(LocalModelRuntimeService::gpuFitTargetMiB(0.0, 24'576) == 512
                  && LocalModelRuntimeService::gpuFitTargetMiB(30.0, 24'576) == 256
                  && LocalModelRuntimeService::gpuFitTargetMiB(8.0, 0) == 512
                  && LocalModelRuntimeService::gpuFitTargetMiB(-1.0, 24'576) == -1,
                  "GPU fit target handles unspecified, capped, and unavailable VRAM");

    model.insert(QStringLiteral("inferenceMode"), QStringLiteral("cpu"));
    ok &= require(LocalModelRuntimeService::llamaServerCommand(model, root, binary, 24'576, &command, &error),
                  "CPU mode command is accepted");
    ok &= require(optionValue(command, QStringLiteral("--device")) == QStringLiteral("none")
                  && optionValue(command, QStringLiteral("--n-gpu-layers")) == QStringLiteral("0")
                  && command.contains(QStringLiteral("--no-kv-offload")),
                  "CPU mode disables device layers and KV offload");

    model.insert(QStringLiteral("inferenceMode"), QStringLiteral("gpu"));
    ok &= require(LocalModelRuntimeService::llamaServerCommand(model, root, binary, 24'576, &command, &error),
                  "GPU mode command is accepted");
    ok &= require(optionValue(command, QStringLiteral("--n-gpu-layers")) == QStringLiteral("all")
                  && !command.contains(QStringLiteral("--fit-target")),
                  "GPU-only mode uses all layers without CPU fit configuration");

    model.insert(QStringLiteral("apiBase"), QStringLiteral("https://example.test/v1"));
    ok &= require(!LocalModelRuntimeService::llamaServerCommand(model, root, binary, 24'576, &command, &error)
                  && error.contains(QStringLiteral("local HTTP")),
                  "non-local API endpoints are rejected");
    model.insert(QStringLiteral("apiBase"), QStringLiteral("http://127.0.0.1:11503/v1/custom"));
    ok &= require(!LocalModelRuntimeService::llamaServerCommand(model, root, binary, 24'576, &command, &error),
                  "custom API paths are rejected");
    model.insert(QStringLiteral("apiBase"), QStringLiteral("http://127.0.0.1:11503/v1"));
    model.insert(QStringLiteral("baseModelPath"), QStringLiteral("missing.gguf"));
    ok &= require(!LocalModelRuntimeService::llamaServerCommand(model, root, binary, 24'576, &command, &error)
                  && error.contains(QStringLiteral("unavailable")),
                  "missing GGUF model is rejected with a useful diagnostic");
    return ok ? 0 : 1;
}
