#pragma once

#include <QObject>
#include <QSettings>

class LanguageSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool darkModeEnabled READ darkModeEnabled WRITE setDarkModeEnabled NOTIFY darkModeEnabledChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowSizeChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowSizeChanged)
    Q_PROPERTY(bool sidebarProjectListExpanded READ sidebarProjectListExpanded WRITE setSidebarProjectListExpanded NOTIFY sidebarProjectListExpandedChanged)
    Q_PROPERTY(int toolOutputPanelWidth READ toolOutputPanelWidth WRITE setToolOutputPanelWidth NOTIFY toolOutputPanelWidthChanged)
    Q_PROPERTY(int toolOutputFontSize READ toolOutputFontSize WRITE setToolOutputFontSize NOTIFY toolOutputFontSizeChanged)
    Q_PROPERTY(int terminalFontSize READ terminalFontSize WRITE setTerminalFontSize NOTIFY terminalFontSizeChanged)
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(QString selectedProjectId READ selectedProjectId WRITE setSelectedProjectId NOTIFY selectedProjectIdChanged)
    Q_PROPERTY(QString chatModelId READ chatModelId WRITE setChatModelId NOTIFY chatModelChanged)
    Q_PROPERTY(QString chatModelApiBase READ chatModelApiBase WRITE setChatModelApiBase NOTIFY chatModelChanged)
    Q_PROPERTY(bool chatMultiAgentEnabled READ chatMultiAgentEnabled WRITE setChatMultiAgentEnabled NOTIFY chatModelChanged)
    Q_PROPERTY(QString editorFontFamily READ editorFontFamily WRITE setEditorFontFamily NOTIFY editorSettingsChanged)
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorSettingsChanged)
    Q_PROPERTY(int editorLineHeight READ editorLineHeight WRITE setEditorLineHeight NOTIFY editorSettingsChanged)
    Q_PROPERTY(int editorTabSize READ editorTabSize WRITE setEditorTabSize NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorInsertSpaces READ editorInsertSpaces WRITE setEditorInsertSpaces NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorWordWrap READ editorWordWrap WRITE setEditorWordWrap NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorSmoothScrolling READ editorSmoothScrolling WRITE setEditorSmoothScrolling NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorShowLineNumbers READ editorShowLineNumbers WRITE setEditorShowLineNumbers NOTIFY editorSettingsChanged)
    Q_PROPERTY(int editorMaximumLines READ editorMaximumLines WRITE setEditorMaximumLines NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorUseVim READ editorUseVim WRITE setEditorUseVim NOTIFY editorSettingsChanged)

public:
    explicit LanguageSettings(QObject *parent = nullptr);

    QString language() const;
    void setLanguage(const QString &value);
    bool darkModeEnabled() const;
    void setDarkModeEnabled(bool value);
    int windowWidth() const;
    void setWindowWidth(int value);
    int windowHeight() const;
    void setWindowHeight(int value);
    Q_INVOKABLE void saveWindowSize(int width, int height);
    bool sidebarProjectListExpanded() const;
    void setSidebarProjectListExpanded(bool value);
    int toolOutputPanelWidth() const;
    void setToolOutputPanelWidth(int value);
    int toolOutputFontSize() const;
    void setToolOutputFontSize(int value);
    int terminalFontSize() const;
    void setTerminalFontSize(int value);
    double uiScale() const;
    void setUiScale(double value);
    QString selectedProjectId() const;
    void setSelectedProjectId(const QString &value);
    QString chatModelId() const;
    void setChatModelId(const QString &value);
    QString chatModelApiBase() const;
    void setChatModelApiBase(const QString &value);
    bool chatMultiAgentEnabled() const;
    void setChatMultiAgentEnabled(bool value);
    QString editorFontFamily() const;
    void setEditorFontFamily(const QString &value);
    int editorFontSize() const;
    void setEditorFontSize(int value);
    int editorLineHeight() const;
    void setEditorLineHeight(int value);
    int editorTabSize() const;
    void setEditorTabSize(int value);
    bool editorInsertSpaces() const;
    void setEditorInsertSpaces(bool value);
    bool editorWordWrap() const;
    void setEditorWordWrap(bool value);
    bool editorSmoothScrolling() const;
    void setEditorSmoothScrolling(bool value);
    bool editorShowLineNumbers() const;
    void setEditorShowLineNumbers(bool value);
    int editorMaximumLines() const;
    void setEditorMaximumLines(int value);
    bool editorUseVim() const;
    void setEditorUseVim(bool value);

    Q_INVOKABLE QString text(const QString &key) const;
    Q_INVOKABLE QString statusText(const QString &status) const;
    Q_INVOKABLE QString phaseText(const QString &phase) const;
    Q_INVOKABLE QString categoryText(const QString &category) const;
    Q_INVOKABLE QString capabilityTitle(const QString &id, const QString &fallback) const;
    Q_INVOKABLE QString capabilityDescription(const QString &id, const QString &fallback) const;

signals:
    void languageChanged();
    void darkModeEnabledChanged();
    void windowSizeChanged();
    void sidebarProjectListExpandedChanged();
    void toolOutputPanelWidthChanged();
    void toolOutputFontSizeChanged();
    void terminalFontSizeChanged();
    void uiScaleChanged();
    void selectedProjectIdChanged();
    void chatModelChanged();
    void editorSettingsChanged();

private:
    // Explicit INI under the user data root; never platform-global QSettings.
    QSettings m_settings;
    QString m_language;
    bool m_darkModeEnabled;
    int m_windowWidth;
    int m_windowHeight;
    bool m_sidebarProjectListExpanded;
    int m_toolOutputPanelWidth;
    int m_toolOutputFontSize;
    int m_terminalFontSize;
    double m_uiScale;
    QString m_selectedProjectId;
    QString m_chatModelId;
    QString m_chatModelApiBase;
    bool m_chatMultiAgentEnabled;
    QString m_editorFontFamily;
    int m_editorFontSize;
    int m_editorLineHeight;
    int m_editorTabSize;
    bool m_editorInsertSpaces;
    bool m_editorWordWrap;
    bool m_editorSmoothScrolling;
    bool m_editorShowLineNumbers;
    int m_editorMaximumLines;
    bool m_editorUseVim;

    bool isEnglish() const;
    static QString normalizedLanguage(const QString &value);
    void saveEditorSettings();
};
