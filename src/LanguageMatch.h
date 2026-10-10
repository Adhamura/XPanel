#pragma once

#include <QString>
#include <QStringList>

namespace LanguageMatch {

// The translation that fits the system's UI languages best. uiLanguages: QLocale::uiLanguages(),
// most preferred first ("de-DE", "pt-BR", "zh-Hans-CN" ...). available: the codes that have a
// translation ("de", "pt_BR", "zh_CN" ...). Returns "en" when English comes first (or nothing
// matches): the source language, no translator needed.
inline QString best(const QStringList &uiLanguages, const QStringList &available)
{
    for (const QString &raw : uiLanguages) {
        const QString code = QString(raw).section('.', 0, 0).section('@', 0, 0).replace('-', '_');
        const QString lang = code.section('_', 0, 0).toLower();
        if (lang == "en" || lang == "c" || lang.isEmpty())
            return QStringLiteral("en");
        for (const QString &a : available) // exact: pt_BR, zh_CN
            if (a.compare(code, Qt::CaseInsensitive) == 0)
                return a;
        if (lang == "zh") { // Simplified Chinese only (zh, zh_Hans, zh_Hans_CN, zh_SG)
            const QString rest = code.mid(2).toLower();
            const bool traditional = rest.contains("hant") || rest.contains("_tw") || rest.contains("_hk")
                                     || rest.contains("_mo");
            if (!traditional && available.contains("zh_CN"))
                return QStringLiteral("zh_CN");
            continue;
        }
        for (const QString &a : available) // the same language: de_AT -> de, pt_PT -> pt_BR
            if (a.section('_', 0, 0).compare(lang, Qt::CaseInsensitive) == 0)
                return a;
    }
    return QStringLiteral("en");
}

} // namespace LanguageMatch
