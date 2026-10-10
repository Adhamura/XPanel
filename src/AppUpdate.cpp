#include "AppUpdate.h"
#include "Config.h"
#include "UpdateLogic.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <memory>

static const char kApiUrl[] = "https://api.github.com/repos/Adhamura/XPanel/releases/latest";
static const char kReleasesPage[] = "https://github.com/Adhamura/XPanel/releases";
static constexpr qint64 kMaxDownload = 1536LL * 1024 * 1024; // far more than any release file
static constexpr qint64 kMaxSmall = 4 * 1024 * 1024;         // API answer, SHA256SUMS

static QString shellQuote(QString s)
{
    return "'" + s.replace("'", "'\\''") + "'";
}

AppUpdate::AppUpdate(Config *config, QObject *parent) : QObject(parent), m_config(config)
{
    m_current = QCoreApplication::applicationVersion();
    if (m_current.isEmpty())
        m_current = XPANEL_VERSION;
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    m_kind = UpdateLogic::installKind(true, appDir, {}, QFile::exists(appDir + "/unins000.exe"), false);
#else
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const bool writable = !appImage.isEmpty() && QFileInfo(QFileInfo(appImage).absolutePath()).isWritable();
    m_kind = UpdateLogic::installKind(false, appDir, appImage, false, writable);
    // a distribution package (Arch PKGBUILD: /usr/bin): the package manager updates it
    m_packageManaged = m_kind == "manual" && appDir == "/usr/bin";
#endif
    // Test hook: XPANEL_UPDATE_URL points the check at another "latest release" JSON, e.g. a
    // local file:// or http://127.0.0.1 server. Only then are local (non-https) downloads allowed.
    m_apiUrl = qEnvironmentVariable("XPANEL_UPDATE_URL");
    m_testUrl = !m_apiUrl.isEmpty();
    if (!m_testUrl)
        m_apiUrl = kApiUrl;

    m_lastCheck = QDateTime::fromString(m_config->get("update.lastCheck").toString(), Qt::ISODate);

    // first look a minute after the start (not while the session is busy starting), then hourly
    // whether the last check is a day old. XPANEL_UPDATE_DELAY (seconds) shortens it for tests.
    bool ok = false;
    const int delay = qEnvironmentVariableIntValue("XPANEL_UPDATE_DELAY", &ok);
    QTimer::singleShot((ok ? qMax(0, delay) : 60) * 1000, this, [this] {
        checkIfDue();
        m_timer.start();
    });
    m_timer.setInterval(60 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &AppUpdate::checkIfDue);
}

AppUpdate::~AppUpdate()
{
    if (m_dlReply)
        m_dlReply->abort();
    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove();
    }
}

bool AppUpdate::available() const
{
    return !m_latest.isEmpty() && UpdateLogic::compareVersions(m_latest, m_current) > 0;
}

bool AppUpdate::skipped() const
{
    return available() && m_config->get("update.skipped").toString() == m_latest;
}

QString AppUpdate::lastCheck() const
{
    // the same format as UpdateCenter::lastCheck
    return m_lastCheck.isValid() ? m_lastCheck.toLocalTime().toString("dd.MM.yyyy HH:mm") : QString();
}

bool AppUpdate::autoCheck() const
{
    return m_config->get("update.autoCheck", true).toBool();
}

void AppUpdate::setAutoCheck(bool on)
{
    if (on == autoCheck())
        return;
    m_config->set("update.autoCheck", on);
    emit changed();
    if (on)
        checkIfDue();
}

void AppUpdate::checkIfDue()
{
    if (!autoCheck() || checking())
        return;
    // at the start: unless checked within the last hour (quick restarts); later: once a day
    const qint64 minAge = m_autoRun ? 23 * 3600 : 3600;
    if (m_lastCheck.isValid() && m_lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < minAge
        && m_lastCheck <= QDateTime::currentDateTimeUtc())
        return;
    m_autoRun = true;
    check();
    if (m_checkReply)
        m_checkReply->setProperty("automatic", true);
}

bool AppUpdate::allowed(const QUrl &url) const
{
    return UpdateLogic::urlAllowed(url, m_testUrl);
}

QNetworkReply *AppUpdate::get(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "XPanel/" + m_current);
    req.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub's download links redirect to *.githubusercontent.com: each hop must pass the
    // allow-list, not only the first URL (and https never goes down to http).
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setMaximumRedirectsAllowed(5);
    req.setTransferTimeout(60000); // no data for a minute: give up
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::redirected, r, [this, r](const QUrl &to) {
        if (allowed(to) && !(r->url().scheme() == "https" && to.scheme() != "https"))
            emit r->redirectAllowed();
        else
            r->abort();
    });
    return r;
}

static QString networkError(QNetworkReply *r)
{
    const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (http == 403 || http == 429)
        return AppUpdate::tr("GitHub's limit for update checks was reached. Try again in an hour.");
    switch (r->error()) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::UnknownNetworkError:
        return AppUpdate::tr("No connection to GitHub. Check the network and try again.");
    case QNetworkReply::OperationCanceledError:
        return AppUpdate::tr("The download was stopped (or redirected to a server other than GitHub).");
    default:
        break;
    }
    if (http >= 400)
        return AppUpdate::tr("GitHub answered with error %1.").arg(http);
    return r->errorString();
}

void AppUpdate::check()
{
    if (checking())
        return;
    if (!allowed(QUrl(m_apiUrl))) {
        m_error = tr("The update address is not allowed: %1").arg(m_apiUrl);
        emit changed();
        return;
    }
    m_error.clear();
    m_checkReply = get(QUrl(m_apiUrl));
    QNetworkReply *r = m_checkReply;
    connect(r, &QNetworkReply::downloadProgress, r, [r](qint64 got, qint64) {
        if (got > kMaxSmall)
            r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r] { finishCheck(r); });
    emit changed();
}

void AppUpdate::finishCheck(QNetworkReply *r)
{
    r->deleteLater();
    m_checkReply = nullptr;
    const bool automatic = r->property("automatic").toBool();
    if (r->error() != QNetworkReply::NoError) {
        m_error = networkError(r);
        qWarning("XPanel: update check failed: %s", qPrintable(r->errorString()));
        emit changed();
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
    const QString tag = o.value("tag_name").toString();
    if (!UpdateLogic::isVersion(tag) || o.value("draft").toBool() || o.value("prerelease").toBool()) {
        m_error = tr("GitHub's answer has no release version.");
        emit changed();
        return;
    }
    m_latest = tag.trimmed();
    if (m_latest.startsWith('v') || m_latest.startsWith('V'))
        m_latest.remove(0, 1);
    const QUrl page(o.value("html_url").toString());
    m_releaseUrl = allowed(page) ? page.toString() : QString(kReleasesPage);
    m_notes = o.value("body").toString().trimmed();
    m_assets.clear();
    for (const QJsonValue &v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        m_assets.insert(a.value("name").toString(), a.value("browser_download_url").toString());
    }
    m_lastCheck = QDateTime::currentDateTimeUtc();
    m_config->set("update.lastCheck", m_lastCheck.toString(Qt::ISODate));
    if (m_state == "failed" && !busy())
        setState("idle");
    emit changed();
    if (available() && !skipped() && m_notified != m_latest) {
        m_notified = m_latest;
        emit updateFound(m_latest, automatic);
    }
}

void AppUpdate::setState(const QString &state, const QString &status)
{
    m_state = state;
    m_status = status;
    if (state != "failed")
        m_error.clear();
    emit changed();
}

void AppUpdate::fail(const QString &why)
{
    if (m_file.isOpen())
        m_file.close();
    if (!m_file.fileName().isEmpty())
        m_file.remove();
    m_state = "failed";
    m_status.clear();
    m_error = why;
    qWarning("XPanel: update failed: %s", qPrintable(why));
    emit changed();
}

QString AppUpdate::updatesDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
}

void AppUpdate::skip()
{
    if (!available())
        return;
    m_config->set("update.skipped", m_latest);
    emit changed();
}

void AppUpdate::openReleasePage()
{
    QDesktopServices::openUrl(QUrl(m_releaseUrl.isEmpty() ? QString(kReleasesPage) : m_releaseUrl));
}

void AppUpdate::copyCommand()
{
    if (!m_command.isEmpty())
        QGuiApplication::clipboard()->setText(m_command);
}

void AppUpdate::cancel()
{
    if (m_state == "downloading" && m_dlReply)
        m_dlReply->abort(); // finished() -> fail()
}

void AppUpdate::update()
{
    if (busy() || !available())
        return;
    if (m_kind == "manual") {
        openReleasePage();
        return;
    }
    const QString name = UpdateLogic::assetName(m_kind, m_latest);
    const QUrl url(m_assets.value(name)), sums(m_assets.value("SHA256SUMS"));
    m_file.setFileName(QString());
    if (!UpdateLogic::safeFileName(name) || url.isEmpty())
        return fail(tr("This release has no file for this installation (%1).").arg(name));
    if (sums.isEmpty())
        return fail(tr("This release has no SHA256SUMS file, so the download cannot be checked. Nothing was installed."));
    if (!allowed(url) || !allowed(sums))
        return fail(tr("The release file is not on GitHub: %1").arg(url.toString()));

    QString dir = updatesDir();
    if (m_kind == "appimage-portable")
        dir = QFileInfo(qEnvironmentVariable("APPIMAGE")).absolutePath(); // next to the running one
    else {
        // old update files: only the newest one is kept
        QDir d(dir);
        for (const QString &f : d.entryList(QDir::Files))
            if (f != name)
                d.remove(f);
    }
    if (!QDir().mkpath(dir))
        return fail(tr("Cannot create the folder %1.").arg(dir));
    m_target = dir + "/" + name;
    m_file.setFileName(m_target + ".part");
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));

    m_command.clear();
    m_newBinary.clear();
    m_progress = 0;
    emit progressChanged();
    setState("downloading", tr("Downloading %1 …").arg(name));
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    m_dlReply = get(url);
    QNetworkReply *r = m_dlReply;
    connect(r, &QNetworkReply::readyRead, this, [this, r, hash] {
        const QByteArray data = r->readAll();
        hash->addData(data);
        if (m_file.write(data) != data.size() || m_file.size() > kMaxDownload)
            r->abort();
    });
    connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? double(got) / double(total) : 0;
        emit progressChanged();
    });
    connect(r, &QNetworkReply::finished, this, [this, r, hash, sums, name] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError)
            return fail(tr("Download failed: %1").arg(networkError(r)));
        const QByteArray rest = r->readAll();
        hash->addData(rest);
        m_file.write(rest);
        if (!m_file.flush()) // disk full ...
            return fail(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
        m_file.close();
        const QByteArray actual = hash->result().toHex();
        // the checksums of every file of the release
        setState("verifying", tr("Checking the download …"));
        QNetworkReply *s = get(sums);
        connect(s, &QNetworkReply::downloadProgress, s, [s](qint64 got, qint64) {
            if (got > kMaxSmall)
                s->abort();
        });
        connect(s, &QNetworkReply::finished, this, [this, s, actual, name] {
            s->deleteLater();
            if (s->error() != QNetworkReply::NoError)
                return fail(tr("Cannot download SHA256SUMS: %1").arg(networkError(s)));
            const QByteArray expected = UpdateLogic::sha256For(s->readAll(), name);
            if (expected.isEmpty())
                return fail(tr("SHA256SUMS does not list %1. Nothing was installed.").arg(name));
            if (expected != actual)
                return fail(tr("The download is damaged: its SHA-256 checksum does not match the release. Nothing was installed."));
            QFile::remove(m_target);
            if (!m_file.rename(m_target))
                return fail(tr("Cannot write %1: %2").arg(m_target, m_file.errorString()));
            m_file.setFileName(QString()); // keep it from now on
            install();
        });
    });
}

QStringList AppUpdate::installFlags() const
{
    // what the first --install was given and a second one does not repeat by itself:
    // --game-mode enables the Steam Game Mode unit for every user (systemctl --global enable).
    // (--disable-hhd is not repeated: hhd stays disabled once it is.)
    QStringList flags;
    if (QFile::exists("/etc/systemd/user/graphical-session.target.wants/xpanel.service"))
        flags << "--game-mode";
    return flags;
}

void AppUpdate::install()
{
    m_progress = 1;
    emit progressChanged();
    if (m_kind == "appimage-portable") {
        QFile::setPermissions(m_target, QFile::permissions(m_target) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        m_newBinary = m_target;
        setState("ready", tr("XPanel %1 was saved as %2. The old file was kept.").arg(m_latest, m_target));
        return;
    }
    if (m_kind == "windows") {
        // /relaunch=1: the installer starts XPanel again when it is done (see xpanel.iss)
        const QStringList args{"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CLOSEAPPLICATIONS", "/relaunch=1"};
        if (!QProcess::startDetached(m_target, args))
            return fail(tr("Cannot start the installer %1.").arg(m_target));
        setState("installing", tr("The installer is running. XPanel closes now and starts again when it is done."));
        QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
        return;
    }
    installSystem();
}

void AppUpdate::installSystem()
{
    QFile::setPermissions(m_target, QFile::permissions(m_target) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    const QStringList flags = installFlags();
    m_command = "sudo " + shellQuote(m_target) + " --install" + (flags.isEmpty() ? QString() : " " + flags.join(' '));
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    if (pkexec.isEmpty())
        return setState("command", tr("pkexec is not installed. Run this command in a terminal to install XPanel %1:").arg(m_latest));
    setState("installing", tr("Installing XPanel %1. Enter your password when asked …").arg(m_latest));
    // pkexec: the system's password prompt (polkit), then the AppImage's own installer as root.
    // It replaces /opt/xpanel, rewrites the systemd units and restarts xpanel-daemon.
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromLocal8Bit(m_proc->readAll()).trimmed();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (st == QProcess::NormalExit && code == 0) {
            m_newBinary = "/opt/xpanel/AppRun";
            m_command.clear();
            return setState("installed", tr("XPanel %1 is installed. Restart XPanel to use it.").arg(m_latest));
        }
        qWarning("XPanel: pkexec install ended with %d: %s", code, qPrintable(out.right(2000)));
        // 126: the password prompt was dismissed; 127: no polkit agent (Steam Game Mode) or not allowed
        if (code == 126 || code == 127)
            setState("command", tr("The password prompt was cancelled or is not available here (Steam Game Mode has none). Run this command in a terminal:"));
        else
            setState("command", tr("The installer stopped with error %1. Run this command in a terminal to see why:").arg(code));
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_proc->deleteLater();
        m_proc = nullptr;
        setState("command", tr("pkexec could not be started. Run this command in a terminal:"));
    });
    m_proc->start(pkexec, QStringList{m_target, "--install"} + flags);
}

void AppUpdate::restart()
{
    if (m_newBinary.isEmpty())
        return;
#ifdef Q_OS_WIN
    QProcess::startDetached(m_newBinary, {"--app", "settings/update"});
    QCoreApplication::quit();
#else
    // Single instance: a new XPanel started while this one runs would only hand its command over.
    // So a small shell waits until this process is gone (at most 10 s), then starts the new one.
    QProcess p;
    p.setProgram("/bin/sh");
    p.setArguments({"-c",
                    "i=0; while kill -0 \"$1\" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done; "
                    "shift; exec \"$@\"",
                    "sh", QString::number(QCoreApplication::applicationPid()), m_newBinary, "--app", "settings/update"});
    // the AppImage runtime of the running copy set these; the new one sets its own
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *v : {"APPIMAGE", "APPDIR", "ARGV0", "OWD"})
        env.remove(QString::fromLatin1(v));
    p.setProcessEnvironment(env);
    p.setStandardOutputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());
    if (!p.startDetached()) {
        fail(tr("Cannot start %1.").arg(m_newBinary));
        return;
    }
    QCoreApplication::quit();
#endif
}
