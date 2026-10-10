#pragma once

#include <QObject>
#include <QVariantList>

class Config;

// KDE-side settings: colour presets (screen colour via KWin Night Color), global keyboard
// shortcuts (KDE kglobalaccel), Feedback notes and App Diagnostics.
class DesktopTweaks : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString colorPreset READ colorPreset NOTIFY colorPresetChanged)

public:
    explicit DesktopTweaks(Config *config, QObject *parent = nullptr);

    // Colour presets: default | cinema | eyecare | night | rts
    QString colorPreset() const;
    Q_INVOKABLE void setColorPreset(const QString &preset);
    Q_INVOKABLE void setWideColor(bool on); // KWin wide colour gamut on the internal screen

    // Shortcuts: action "quickpanel" or "controlcenter"; keys like "Meta+G"
    Q_INVOKABLE QString shortcut(const QString &action) const;
    Q_INVOKABLE bool setShortcut(const QString &action, const QString &keys);
    void registerShortcuts();

    Q_INVOKABLE bool saveFeedback(const QString &topic, const QString &text);
    Q_INVOKABLE QString feedbackPath() const;
    Q_INVOKABLE QVariantList diagnostics() const;
    Q_INVOKABLE void openSystemSettings(const QString &module); // e.g. kcm_users, kcm_keys

signals:
    void colorPresetChanged();
    void shortcutPressed(const QString &action);

private slots:
    void onGlobalShortcut(const QString &component, const QString &action, qlonglong timestamp);

private:
    Config *m_config;
};
