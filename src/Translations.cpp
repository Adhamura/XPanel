#include "Translations.h"
#include "Config.h"
#include "LanguageMatch.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>

namespace {

// Every language with a translation file (CMakeLists.txt: i18n/xpanel_<code>.ts). The names are
// the languages' own and are not translated.
struct Language {
    const char *code;
    const char *name;
};
const Language kLanguages[] = {
    {"de", "Deutsch"},    {"es", "Español"},  {"fr", "Français"},          {"it", "Italiano"},
    {"ja", "日本語"},      {"ko", "한국어"},    {"pt_BR", "Português (Brasil)"}, {"ru", "Русский"},
    {"zh_CN", "简体中文"},
};

// qt_add_translations(... RESOURCE_PREFIX /i18n) puts the compiled files here
QString resourcePath(const QString &code)
{
    return ":/i18n/xpanel_" + code + ".qm";
}

} // namespace

Translations::Translations(Config *config, QObject *parent) : QObject(parent), m_config(config) {}

QString Translations::language() const
{
    return m_config->get("language", "").toString();
}

// Languages whose .qm holds translations: a language that is still all "unfinished" in its .ts
// file compiles to an empty catalog and is not offered yet.
QStringList Translations::available() const
{
    QStringList out;
    for (const Language &l : kLanguages) {
        QTranslator t;
        if (QFile::exists(resourcePath(l.code)) && t.load(resourcePath(l.code)) && !t.isEmpty())
            out << l.code;
    }
    return out;
}

QVariantList Translations::languages() const
{
    QVariantList out;
    out << QVariantMap{{"code", "en"}, {"name", "English"}};
    const QStringList have = available();
    for (const Language &l : kLanguages)
        if (have.contains(l.code))
            out << QVariantMap{{"code", l.code}, {"name", QString::fromUtf8(l.name)}};
    return out;
}

void Translations::setLanguage(const QString &code)
{
    if (code == language())
        return;
    if (!code.isEmpty() && code != "en" && !available().contains(code))
        return;
    m_config->set("language", code);
    apply();
    emit languageChanged();
}

QString Translations::effective() const
{
    QString code = language();
    if (code.isEmpty())
        code = qEnvironmentVariable("XPANEL_LANGUAGE");
    if (code.isEmpty())
        return LanguageMatch::best(QLocale::system().uiLanguages(), available());
    if (code != "en" && !available().contains(code))
        return "en"; // a language this build does not have
    return code;
}

void Translations::apply()
{
    QCoreApplication::removeTranslator(&m_translator);
    m_current = effective();
    if (m_current != "en") {
        if (m_translator.load(resourcePath(m_current)))
            QCoreApplication::installTranslator(&m_translator);
        else
            qWarning("XPanel: cannot load the %s translation", qPrintable(m_current));
    }
    // dates, numbers: the chosen language, or the system's own settings
    QLocale::setDefault(language().isEmpty() ? QLocale::system() : QLocale(m_current));
    if (m_engine)
        m_engine->retranslate();
}
