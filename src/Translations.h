#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>
#include <QTranslator>
#include <QVariantList>

class Config;

// The UI language. The translations are compiled into the binary (i18n/xpanel_<code>.ts ->
// ":/i18n/xpanel_<code>.qm"); the choice is saved in the config as "language":
//   ""        follow the system (QLocale::system().uiLanguages())
//   "en"      English (the source language, no translator)
//   "de" ...  a translation, e.g. "pt_BR", "zh_CN"
// XPANEL_LANGUAGE=<code> overrides the system language (testing; a saved choice still wins).
// "i18n" context property for QML.
class Translations : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    // the language in use: "en", "de", ...
    Q_PROPERTY(QString current READ current NOTIFY languageChanged)
    // [{code, name}]: English and every translation in the binary; names are native, never translated
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)

public:
    explicit Translations(Config *config, QObject *parent = nullptr);

    // Retranslates this engine's QML (bindings with qsTr) whenever the language changes.
    void setEngine(QQmlEngine *engine) { m_engine = engine; }

    QString language() const;
    void setLanguage(const QString &code);
    QString current() const { return m_current; }
    QVariantList languages() const;
    QStringList available() const; // codes with a compiled translation

    // Install the translator for the saved language. Called once at startup, before any UI exists.
    void apply();

signals:
    void languageChanged();

private:
    QString effective() const;

    Config *m_config;
    QPointer<QQmlEngine> m_engine;
    QTranslator m_translator;
    QString m_current = "en";
};
