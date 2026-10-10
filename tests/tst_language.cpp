// Unit tests for the choice of the UI language (src/LanguageMatch.h).
#include "LanguageMatch.h"

#include <QTest>

class TestLanguage : public QObject {
    Q_OBJECT

private slots:
    void best_data()
    {
        QTest::addColumn<QStringList>("ui");
        QTest::addColumn<QString>("expected");
        const QStringList all{"de", "es", "fr", "it", "ja", "ko", "pt_BR", "ru", "zh_CN"};
        Q_UNUSED(all)
        QTest::newRow("exact language") << QStringList{"de"} << "de";
        QTest::newRow("language and country") << QStringList{"de-DE", "en"} << "de";
        QTest::newRow("austrian german") << QStringList{"de-AT"} << "de";
        QTest::newRow("posix style") << QStringList{"fr_FR.UTF-8"} << "fr";
        QTest::newRow("brazilian") << QStringList{"pt-BR"} << "pt_BR";
        QTest::newRow("portugal uses the only portuguese") << QStringList{"pt-PT"} << "pt_BR";
        QTest::newRow("plain portuguese") << QStringList{"pt"} << "pt_BR";
        QTest::newRow("simplified chinese") << QStringList{"zh-CN"} << "zh_CN";
        QTest::newRow("chinese with script") << QStringList{"zh-Hans-CN"} << "zh_CN";
        QTest::newRow("chinese singapore") << QStringList{"zh-SG"} << "zh_CN";
        QTest::newRow("plain chinese") << QStringList{"zh"} << "zh_CN";
        QTest::newRow("traditional chinese is not simplified") << QStringList{"zh-TW"} << "en";
        QTest::newRow("traditional chinese script") << QStringList{"zh-Hant"} << "en";
        QTest::newRow("english first") << QStringList{"en-US", "de-DE"} << "en";
        QTest::newRow("first one that exists") << QStringList{"sw", "ja-JP", "en"} << "ja";
        QTest::newRow("c locale") << QStringList{"C", "de"} << "en";
        QTest::newRow("nothing matches") << QStringList{"sw-KE", "nl"} << "en";
        QTest::newRow("empty") << QStringList{} << "en";
    }
    void best()
    {
        QFETCH(QStringList, ui);
        QFETCH(QString, expected);
        const QStringList all{"de", "es", "fr", "it", "ja", "ko", "pt_BR", "ru", "zh_CN"};
        QCOMPARE(LanguageMatch::best(ui, all), expected);
    }
    void onlyWhatTheBuildHas()
    {
        QCOMPARE(LanguageMatch::best({"fr-FR", "de"}, {"de"}), QString("de"));
        QCOMPARE(LanguageMatch::best({"fr-FR"}, {}), QString("en"));
        QCOMPARE(LanguageMatch::best({"zh-CN"}, {"de"}), QString("en"));
    }
};

QTEST_GUILESS_MAIN(TestLanguage)
#include "tst_language.moc"
