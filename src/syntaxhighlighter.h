#pragma once

#include <QSyntaxHighlighter>

class QQuickTextDocument;

class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QObject *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)

public:
    explicit SyntaxHighlighter(QObject *parent = nullptr);

    QObject *textDocument() const;
    void setTextDocument(QObject *textDocument);
    QString language() const;
    void setLanguage(const QString &language);
    int lineHeight() const;
    void setLineHeight(int value);

signals:
    void textDocumentChanged();
    void languageChanged();
    void lineHeightChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void applyLineHeight();
    void scheduleLineHeightUpdate();
    void applyPattern(const QString &text, const QRegularExpression &expression, const QTextCharFormat &format);

    QQuickTextDocument *m_textDocument = nullptr;
    QString m_language;
    int m_lineHeight = 19;
    bool m_lineHeightUpdatePending = false;
};
