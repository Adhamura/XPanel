#pragma once

#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply> // QPointer<QNetworkReply> needs the complete type (Qt 6.8)
#include <QObject>
#include <QPointer>
#include <QTimer>

class Config;
class QNetworkReply;
class QProcess;

// XPanel's own updates (Settings > Update Center > XPanel):
//   check    GET api.github.com/repos/Adhamura/XPanel/releases/latest, once a minute after the start
//            and then once a day ("update.autoCheck", default on), or on request
//   update   download the release file for this install, check its SHA-256 against the release's
//            SHA256SUMS, then install it:
//              appimage-installed  pkexec <new AppImage> --install   (replaces /opt/xpanel)
//              appimage-portable   the new AppImage next to the old one (the old one is kept)
//              windows             the Setup.exe, silent (UAC prompt), XPanel quits for it
//              manual              nothing: the release page and what to do
// XPANEL_UPDATE_URL overrides the API URL (tests: a local file or http://127.0.0.1 server).
class AppUpdate : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString installKind READ installKind CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool skipped READ skipped NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(QString lastCheck READ lastCheck NOTIFY changed)
    // idle, downloading, verifying, installing, installed (restart offered), ready (restart offered),
    // command (run manualCommand in a terminal), failed
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString manualCommand READ manualCommand NOTIFY changed)
    Q_PROPERTY(bool packageManaged READ packageManaged CONSTANT)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)

public:
    explicit AppUpdate(Config *config, QObject *parent = nullptr);
    ~AppUpdate() override;

    QString currentVersion() const { return m_current; }
    QString installKind() const { return m_kind; }
    QString latestVersion() const { return m_latest; }
    bool available() const;
    bool skipped() const;
    QString notes() const { return m_notes; }
    QString releaseUrl() const { return m_releaseUrl; }
    bool checking() const { return m_checkReply != nullptr; }
    bool checked() const { return !m_latest.isEmpty(); } // in this session
    QString lastCheck() const;
    QString state() const { return m_state; }
    bool busy() const { return m_state == "downloading" || m_state == "verifying" || m_state == "installing"; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    double progress() const { return m_progress; }
    QString manualCommand() const { return m_command; }
    bool packageManaged() const { return m_packageManaged; }
    bool autoCheck() const;
    void setAutoCheck(bool on);

    Q_INVOKABLE void check();
    Q_INVOKABLE void update();      // download, verify, install
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void skip();        // no reminder for this version
    Q_INVOKABLE void restart();     // into the new version
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void copyCommand();

signals:
    void changed();
    void progressChanged();
    // a newer version than the running one (not skipped); `automatic`: found by the daily check
    void updateFound(const QString &version, bool automatic);

private:
    void checkIfDue();
    void finishCheck(QNetworkReply *r);
    QNetworkReply *get(const QUrl &url);
    bool allowed(const QUrl &url) const;
    void download();
    void downloadSums();
    void verify(const QByteArray &sums);
    void install();
    void installSystem(); // pkexec
    void fail(const QString &why);
    void setState(const QString &state, const QString &status = {});
    QString updatesDir() const;
    QStringList installFlags() const;

    Config *m_config;
    QNetworkAccessManager m_net;
    QTimer m_timer;
    QString m_current, m_kind, m_apiUrl;
    bool m_testUrl = false, m_packageManaged = false;
    QString m_latest, m_notes, m_releaseUrl;
    QMap<QString, QString> m_assets; // name -> browser_download_url
    QDateTime m_lastCheck;
    bool m_autoRun = false;
    QString m_notified; // version announced by updateFound already
    QPointer<QNetworkReply> m_checkReply, m_dlReply;
    QFile m_file;          // the download in progress (<target>.part)
    QString m_target;      // the verified file ends up here
    QString m_newBinary;   // what restart() starts
    QString m_state = "idle", m_status, m_error, m_command;
    double m_progress = 0;
    QProcess *m_proc = nullptr;
};
