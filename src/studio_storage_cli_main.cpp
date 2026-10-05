#include "studiopaths.h"
#include "studiouserdata.h"
#include "studionativeapi.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dft-agent-studio-storage"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption rootOption(QStringList{QStringLiteral("agent-root")},
        QStringLiteral("Studio checkout root."), QStringLiteral("path"));
    QCommandLineOption methodOption(QStringList{QStringLiteral("method")},
        QStringLiteral("Native Studio storage method."), QStringLiteral("method"));
    parser.addOption(rootOption);
    parser.addOption(methodOption);
    parser.process(application);

    const QString method = parser.value(methodOption).trimmed();
    const QString root = parser.value(rootOption).trimmed().isEmpty()
        ? studioFindAgentRoot() : QDir(parser.value(rootOption)).absolutePath();
    QString dataError;
    if (!initializeStudioUserDataRoot(root, &dataError)) {
        QTextStream(stdout) << QJsonDocument(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), dataError},
        }).toJson(QJsonDocument::Compact) << Qt::endl;
        return 2;
    }
    QFile inputFile;
    const bool inputOpened = inputFile.open(stdin, QIODevice::ReadOnly);
    const QByteArray input = inputOpened ? inputFile.readAll() : QByteArray{};
    QJsonParseError parseError{};
    const QJsonDocument paramsDocument = QJsonDocument::fromJson(input, &parseError);
    QVariantMap response;
    if (method.isEmpty()) {
        response = {{QStringLiteral("ok"), false},
                    {QStringLiteral("message"), QStringLiteral("--method is required.")}};
    } else if (!inputOpened || parseError.error != QJsonParseError::NoError
               || (!paramsDocument.isObject() && !input.trimmed().isEmpty())) {
        response = {{QStringLiteral("ok"), false},
                    {QStringLiteral("message"), QStringLiteral("stdin must contain a JSON object: %1")
                         .arg(parseError.errorString())}};
    } else {
        response = StudioNativeApi::dispatch(method,
            paramsDocument.isObject() ? paramsDocument.object().toVariantMap() : QVariantMap{}, root);
    }
    QTextStream output(stdout);
    output << QJsonDocument(QJsonObject::fromVariantMap(response)).toJson(QJsonDocument::Compact)
           << Qt::endl;
    return response.value(QStringLiteral("ok")).toBool() ? 0 : 2;
}
