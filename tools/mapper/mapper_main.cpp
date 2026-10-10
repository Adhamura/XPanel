// xpanel-mapper - find out which Ally X input node / code each physical control sends.
// Starts xpanel-input-probe as root (sudo -n, else pkexec), lists every unique input,
// and lets you click the matching control on a front / back picture of the Ally X.
// The result is saved as JSON for XPanel's own input layer.
//
// Usage: xpanel-mapper [mapping.json]   (default: ~/.config/XPanel/input-map.json)

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString mapPath READ mapPath CONSTANT)

public:
    explicit Backend(const QString &mapPath, QObject *parent = nullptr)
        : QObject(parent), m_mapPath(mapPath)
    {
        connect(&m_proc, &QProcess::readyReadStandardOutput, this, &Backend::onOutput);
        connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
            const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
            if (!err.isEmpty())
                emit status(err);
        });
        connect(&m_proc, &QProcess::stateChanged, this, &Backend::runningChanged);
        connect(&m_proc, &QProcess::finished, this, [this](int code) {
            emit status(QString("probe stopped (exit %1), InputPlumber is running again").arg(code));
        });
    }

    ~Backend() override { stop(); }

    bool running() const { return m_proc.state() != QProcess::NotRunning; }
    QString mapPath() const { return m_mapPath; }

    Q_INVOKABLE void start()
    {
        if (running())
            return;
        const QString probe = QCoreApplication::applicationDirPath() + "/xpanel-input-probe";
        // Passwordless sudo when allowed, otherwise the polkit password dialog.
        const bool sudoOk = QProcess::execute("sudo", {"-n", "true"}) == 0;
        if (sudoOk)
            m_proc.start("sudo", {"-n", probe});
        else
            m_proc.start("pkexec", {probe});
        emit status(sudoOk ? "starting probe (sudo)" : "starting probe (pkexec: enter your password)");
    }

    Q_INVOKABLE void stop()
    {
        if (!running())
            return;
        m_proc.terminate(); // sudo/pkexec pass SIGTERM on; the probe starts InputPlumber again
        if (!m_proc.waitForFinished(4000))
            m_proc.kill();
    }

    Q_INVOKABLE QVariantList load() const
    {
        QFile f(m_mapPath);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value("inputs").toArray().toVariantList();
    }

    Q_INVOKABLE bool save(const QVariantList &rows)
    {
        QDir().mkpath(QFileInfo(m_mapPath).absolutePath());
        QJsonObject root;
        root["device"] = "ASUS ROG Ally X (0b05:1b4c)";
        root["inputs"] = QJsonArray::fromVariantList(rows);
        QSaveFile f(m_mapPath);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(QJsonDocument(root).toJson());
        const bool ok = f.commit() && writeTable(rows);
        emit status(ok ? QString("saved %1 inputs to %2 (+ .md table)").arg(rows.size()).arg(m_mapPath)
                       : "save failed: " + m_mapPath);
        return ok;
    }

    // Readable copy next to the JSON: one table row per mapped input.
    bool writeTable(const QVariantList &rows) const
    {
        QString md = "# ROG Ally X input map\n\n"
                     "Made with `tools/mapper` (XPanel Button Mapper). Machine copy: `" +
                     QFileInfo(m_mapPath).fileName() + "`.\n\n"
                     "| Control | Variant | Kind | Node | Device | Code / report |\n"
                     "|---|---|---|---|---|---|\n";
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            const QString code = r["kind"] == "hid"
                ? "report " + r["hidId"].toString() + " `" + r["hidPrefix"].toString() + "`"
                : r["name"].toString() + " (" + r["code"].toString() + ")";
            md += QString("| %1 | %2 | %3 | %4 | %5 | %6 |\n")
                      .arg(r["control"].toString(), r["variant"].toString(), r["kind"].toString(),
                           r["src"].toString(), r["dev"].toString(), code);
        }
        QString mdPath = m_mapPath;
        mdPath.replace(QRegularExpression("\\.json$"), ".md");
        QSaveFile f(mdPath);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(md.toUtf8());
        return f.commit();
    }

signals:
    void runningChanged();
    void status(const QString &text);
    void input(const QVariantMap &ev);

private:
    void onOutput()
    {
        while (m_proc.canReadLine()) {
            const QVariantMap ev = QJsonDocument::fromJson(m_proc.readLine()).object().toVariantMap();
            const QString kind = ev.value("kind").toString();
            if (kind == "alive" || kind.isEmpty())
                continue;
            if (kind == "status")
                emit status(ev.value("text").toString());
            else
                emit input(ev);
        }
    }

    QString m_mapPath;
    QProcess m_proc;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("XPanel Button Mapper");
    const QString mapPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                                           "/XPanel/input-map.json";
    Backend backend(mapPath);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::stop);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/mapper/Mapper.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}

#include "mapper_main.moc"
