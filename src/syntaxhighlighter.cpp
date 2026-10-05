#include "syntaxhighlighter.h"

#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

namespace {
QTextCharFormat textFormat(const QColor &color, QFont::Weight weight = QFont::Normal) {
    QTextCharFormat format;
    format.setForeground(color);
    format.setFontWeight(weight);
    return format;
}

const QRegularExpression StringPattern(QStringLiteral(R"(("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'))"));
const QRegularExpression NumberPattern(QStringLiteral(R"(\b(?:\d+(?:\.\d+)?(?:ns|ps|MHz|GHz)?|'[bBoOdDhH][0-9a-fA-F_xXzZ]+)\b)"));
const QRegularExpression VerilogCommentPattern(QStringLiteral(R"(//[^\n]*|/\*.*\*/)"));
const QRegularExpression TclCommentPattern(QStringLiteral(R"(^\s*#.*$)"));
const QRegularExpression PreprocessorPattern(QStringLiteral(R"(^\s*`\w+)"));
const QRegularExpression VerilogKeywordPattern(QStringLiteral(
    R"(\b(?:always(?:_comb|_ff|_latch)?|assign|begin|case|casex|casez|default|else|end|endcase|endmodule|endfunction|endgenerate|endtask|for|foreach|function|generate|if|import|inout|input|integer|interface|localparam|logic|module|output|package|parameter|reg|return|signed|struct|task|typedef|union|unsigned|wire|while)\b)"
));
const QRegularExpression TclKeywordPattern(QStringLiteral(
    R"(\b(?:analyze|compile(?:_ultra)?|create_clock|create_generated_clock|create_test_protocol|current_design|elab|find|foreach|get_cells|get_clocks|get_pins|get_ports|insert_dft|link|read_ddc|read_file|report_area|report_clock|report_dft|report_timing|set|set_case_analysis|set_clock_uncertainty|set_dft_configuration|set_dft_signal|set_false_path|set_input_delay|set_output_delay|set_path|set_search_path|set_target_library|source|write)\b)"
));
const QRegularExpression FilelistDirectivePattern(QStringLiteral(R"(^\s*(?:\+incdir\+|\+define\+|-f|-y).*$)"));
}

SyntaxHighlighter::SyntaxHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent) {
}

QObject *SyntaxHighlighter::textDocument() const {
    return m_textDocument;
}

void SyntaxHighlighter::setTextDocument(QObject *textDocument) {
    auto *nextDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (m_textDocument == nextDocument)
        return;
    m_textDocument = nextDocument;
    QSyntaxHighlighter::setDocument(m_textDocument ? m_textDocument->textDocument() : nullptr);
    if (document())
        connect(document(), &QTextDocument::contentsChanged, this, &SyntaxHighlighter::scheduleLineHeightUpdate, Qt::UniqueConnection);
    emit textDocumentChanged();
    rehighlight();
    scheduleLineHeightUpdate();
}

QString SyntaxHighlighter::language() const {
    return m_language;
}

void SyntaxHighlighter::setLanguage(const QString &language) {
    const QString normalized = language.trimmed().toLower();
    if (m_language == normalized)
        return;
    m_language = normalized;
    emit languageChanged();
    rehighlight();
}

int SyntaxHighlighter::lineHeight() const {
    return m_lineHeight;
}

void SyntaxHighlighter::setLineHeight(int value) {
    const int normalized = qBound(12, value, 42);
    if (m_lineHeight == normalized)
        return;
    m_lineHeight = normalized;
    emit lineHeightChanged();
    applyLineHeight();
}

void SyntaxHighlighter::scheduleLineHeightUpdate() {
    if (m_lineHeightUpdatePending)
        return;
    m_lineHeightUpdatePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_lineHeightUpdatePending = false;
        applyLineHeight();
    });
}

void SyntaxHighlighter::applyLineHeight() {
    auto *currentDocument = document();
    if (!currentDocument)
        return;

    for (QTextBlock block = currentDocument->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (format.lineHeight() == m_lineHeight
            && format.lineHeightType() == QTextBlockFormat::FixedHeight)
            continue;
        format.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text) {
    setFormat(0, text.size(), QTextCharFormat{});

    const auto stringFormat = textFormat(QColor(QStringLiteral("#ce9178")));
    const auto numberFormat = textFormat(QColor(QStringLiteral("#b5cea8")));
    const auto commentFormat = textFormat(QColor(QStringLiteral("#6a9955")));
    const auto keywordFormat = textFormat(QColor(QStringLiteral("#569cd6")), QFont::DemiBold);
    const auto commandFormat = textFormat(QColor(QStringLiteral("#c586c0")), QFont::DemiBold);
    const auto preprocessorFormat = textFormat(QColor(QStringLiteral("#c586c0")));

    if (m_language == QStringLiteral("verilog") || m_language == QStringLiteral("systemverilog")) {
        applyPattern(text, VerilogKeywordPattern, keywordFormat);
        applyPattern(text, PreprocessorPattern, preprocessorFormat);
        applyPattern(text, NumberPattern, numberFormat);
        applyPattern(text, StringPattern, stringFormat);
        applyPattern(text, VerilogCommentPattern, commentFormat);
        return;
    }

    if (m_language == QStringLiteral("tcl") || m_language == QStringLiteral("sdc")) {
        applyPattern(text, TclKeywordPattern, commandFormat);
        applyPattern(text, NumberPattern, numberFormat);
        applyPattern(text, StringPattern, stringFormat);
        applyPattern(text, TclCommentPattern, commentFormat);
        return;
    }

    if (m_language == QStringLiteral("filelist") || m_language == QStringLiteral("config")) {
        applyPattern(text, FilelistDirectivePattern, commandFormat);
        applyPattern(text, TclCommentPattern, commentFormat);
    }
}

void SyntaxHighlighter::applyPattern(const QString &text, const QRegularExpression &expression, const QTextCharFormat &format) {
    auto match = expression.globalMatch(text);
    while (match.hasNext()) {
        const auto current = match.next();
        setFormat(current.capturedStart(), current.capturedLength(), format);
    }
}
