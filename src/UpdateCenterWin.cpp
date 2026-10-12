// Update Center, Windows backend: ASUS BIOS / MCU / driver lists (rog.asus.com web API, the same one
// G-Helper uses), verified downloads and installs (elevated through the UAC prompt, only after a SHA-256 match).
#include <QtGlobal>

#ifdef Q_OS_WIN

#include "UpdateCenter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <vector>

#include <windows.h>
#include <setupapi.h>
#include <shellapi.h>

namespace {

const char kApiHost[] = "https://rog.asus.com/support/webapi/product/";

// numeric parts of a version: "V315" -> 315, "1.2.0.45" -> 1,2,0,45
QList<int> versionParts(const QString &v)
{
    QList<int> out;
    const QStringList parts = v.split(QRegularExpression("[^0-9]+"), Qt::SkipEmptyParts);
    for (const QString &p : parts)
        out << p.left(9).toInt();
    return out;
}

int compareVersions(const QString &a, const QString &b)
{
    QList<int> x = versionParts(a), y = versionParts(b);
    const qsizetype n = qMax(x.size(), y.size());
    for (qsizetype i = 0; i < n; ++i) {
        const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}

QString statusFor(const QString &installed, const QString &latest)
{
    if (installed.isEmpty() || versionParts(installed).isEmpty() || versionParts(latest).isEmpty())
        return "unknown";
    const int c = compareVersions(latest, installed);
    return c > 0 ? "update" : (c < 0 ? "newer" : "current");
}

// newest file per "category|title"
QMap<QString, QPair<QString, QJsonObject>> newestFiles(const QJsonArray &obj)
{
    QMap<QString, QPair<QString, QJsonObject>> newest;
    for (const QJsonValue &c : obj) {
        const QString cat = c.toObject().value("Name").toString();
        for (const QJsonValue &fv : c.toObject().value("Files").toArray()) {
            const QJsonObject file = fv.toObject();
            const QString key = cat + "|" + file.value("Title").toString();
            if (!newest.contains(key)
                || compareVersions(file.value("Version").toString(), newest[key].second.value("Version").toString()) > 0)
                newest[key] = {cat, file};
        }
    }
    return newest;
}

bool isTrue(const QJsonValue &v)
{
    return v.isBool() ? v.toBool() : v.toVariant().toString().trimmed().compare("true", Qt::CaseInsensitive) == 0;
}

QVariantMap rowFor(const QString &name, const QString &category, const QJsonObject &file, const QString &installed,
                   const QString &folder)
{
    const QString latest = file.value("Version").toString();
    const QString status = statusFor(installed, latest);
    const QString url = file.value("DownloadUrl").toObject().value("Global").toString().trimmed();
    return QVariantMap{{"name", name},
                       {"title", file.value("Title").toString()},
                       {"category", category},
                       {"installed", installed},
                       {"latest", latest},
                       {"update", status == "update" ? latest : QString()},
                       {"date", file.value("ReleaseDate").toString()},
                       {"size", file.value("FileSize").toString()},
                       {"url", url},
                       {"sha256", file.value("sha256").toString().trimmed()},
                       {"status", status},
                       {"folder", folder},
                       {"exeModule", file.value("ExeModule").toString().trimmed()},
                       {"reboot", file.value("Reboot").toVariant().toString().trimmed()},
                       {"acPower", isTrue(file.value("Ac_power"))},
                       {"isStore", !url.isEmpty() && !url.startsWith("http", Qt::CaseInsensitive)}};
}

QString lastSection(const QString &v)
{
    return v.trimmed().section('.', -1);
}

// A present device: hardware IDs (lower case) and its driver version.
struct Device {
    QStringList ids;
    QString driverVersion;
};

QList<Device> presentDevices()
{
    // DEVPKEY_Device_DriverVersion {a8b865dd-2e3d-4094-ad97-e593a70c75d6}, 3 (written out: no initguid needed)
    static const DEVPROPKEY keyDriverVersion = {
        {0xa8b865dd, 0x2e3d, 0x4094, {0xad, 0x97, 0xe5, 0x93, 0xa7, 0x0c, 0x75, 0xd6}}, 3};

    QList<Device> out;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return out;
    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        std::vector<wchar_t> ids(8192, L'\0');
        DWORD type = 0, need = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, &type, reinterpret_cast<PBYTE>(ids.data()),
                                               DWORD((ids.size() - 1) * sizeof(wchar_t)), &need))
            continue;
        Device d;
        for (const wchar_t *p = ids.data(); *p; p += wcslen(p) + 1)
            d.ids << QString::fromWCharArray(p).toLower();
        wchar_t ver[256] = {0};
        DEVPROPTYPE ptype = 0;
        DWORD vneed = 0;
        if (SetupDiGetDevicePropertyW(set, &info, &keyDriverVersion, &ptype, reinterpret_cast<PBYTE>(ver),
                                      sizeof(ver) - sizeof(wchar_t), &vneed, 0)
            && ptype == DEVPROP_TYPE_STRING)
            d.driverVersion = QString::fromWCharArray(ver);
        if (!d.ids.isEmpty() && !d.driverVersion.isEmpty())
            out << d;
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

QString safeName(const QString &s)
{
    QString r = s;
    r.replace(QRegularExpression("[^0-9A-Za-z._-]+"), "_");
    while (r.startsWith('.'))
        r.remove(0, 1);
    return r.left(80);
}

void showInExplorer(const QString &path)
{
    QProcess p;
    p.setProgram("explorer.exe");
    p.setNativeArguments("/select,\"" + QDir::toNativeSeparators(path) + "\"");
    p.startDetached();
}

} // namespace

void UpdateCenter::checkWin()
{
    m_allyFw.clear();
    m_drivers.clear();
    m_pending = 2;
    emit changed();
    checkAsusWin();
    checkDriversWin();
}

// GET with redirects; if the answer has no Result.Obj, ask once more with a random "tag" (cache buster).
void UpdateCenter::winFetch(const QString &baseUrl, bool canRetry,
                            std::function<void(const QJsonArray &, const QString &)> cb)
{
    QNetworkRequest req{QUrl(baseUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "XPanel/" XPANEL_VERSION);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrl, canRetry, cb] {
        reply->deleteLater();
        const QString err = reply->error() != QNetworkReply::NoError ? reply->errorString() : QString();
        const QJsonArray obj = QJsonDocument::fromJson(reply->readAll()).object().value("Result").toObject().value("Obj").toArray();
        if (obj.isEmpty() && canRetry) {
            winFetch(baseUrl + "&tag=" + QString::number(QRandomGenerator::global()->bounded(10, 100)), false, cb);
            return;
        }
        cb(obj, err);
    });
}

void UpdateCenter::checkAsusWin()
{
    QSettings bios("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS", QSettings::NativeFormat);
    const QString model = bios.value("BaseBoardProduct").toString().trimmed();
    QString biosNow = lastSection(bios.value("BIOSVersion").toString());
    bool ok = false;
    const int biosInt = biosNow.toInt(&ok);
    if (ok)
        biosNow = QString::number(biosInt);

    // MCU: ROGKBFW "CurrentVersion" (what Armoury Crate shows), then ROGMCUFW (last '.' part)
    QString mcuNow;
    {
        QSettings kb("HKEY_LOCAL_MACHINE\\SOFTWARE\\ASUS\\FWVersion\\ROGKBFW", QSettings::NativeFormat);
        mcuNow = kb.value("CurrentVersion").toString().trimmed(); // DWORD or string
        if (mcuNow.isEmpty()) {
            QSettings mcuReg("HKEY_LOCAL_MACHINE\\SOFTWARE\\ASUS\\FWVersion\\ROGMCUFW", QSettings::NativeFormat);
            QString v = mcuReg.value("Main FW version").toString();
            if (v.trimmed().isEmpty())
                v = mcuReg.value("FW version").toString();
            mcuNow = lastSection(v);
        }
    }

    if (model.isEmpty()) {
        m_allyFw << QVariantMap{{"name", tr("ASUS support site")}, {"title", QString()}, {"status", "unknown"},
                                {"how", tr("Cannot read the model from the registry.")}};
        done();
        return;
    }
    const QString url = QString::fromLatin1(kApiHost) + "GetPDBIOS?website=global&model=" + QUrl::toPercentEncoding(model)
                        + "&cpu=" + QUrl::toPercentEncoding(model) + "&systemCode=rog";
    winFetch(url, true, [this, model, biosNow, mcuNow](const QJsonArray &obj, const QString &err) {
        m_allyFw.clear();
        QVariantList biosRows, mcuRows, otherRows;
        const auto newest = newestFiles(obj);
        for (auto it = newest.begin(); it != newest.end(); ++it) {
            const QString cat = it.value().first;
            const QJsonObject file = it.value().second;
            const QString title = file.value("Title").toString();
            if (cat.contains("Windows", Qt::CaseInsensitive)) {
                QVariantMap r = rowFor("BIOS", cat, file, biosNow, "BIOS");
                r["bios"] = true;
                r["how"] = tr("Run the downloaded installer yourself. Keep the charger connected.");
                biosRows << r;
            } else if (cat.compare("BIOS", Qt::CaseInsensitive) == 0) {
                continue; // EZ Flash zip: for Linux, the Windows installer is used here
            } else if (title.contains("MCU", Qt::CaseInsensitive)) {
                QVariantMap r = rowFor(tr("Controller (MCU)"), cat, file, mcuNow, "MCU");
                r["bios"] = false;
                r["how"] = tr("Run the downloaded tool yourself. Keep the charger connected.");
                mcuRows << r;
            } else {
                QVariantMap r = rowFor(title, cat, file, QString(), safeName(title));
                r["bios"] = false;
                r["how"] = tr("Run the downloaded file yourself.");
                otherRows << r;
            }
        }
        m_allyFw = biosRows + mcuRows + otherRows;
        if (m_allyFw.isEmpty())
            m_allyFw << QVariantMap{{"name", tr("ASUS support site")}, {"title", model}, {"status", "unknown"},
                                    {"how", !err.isEmpty() ? tr("Not reachable: %1").arg(err) : tr("No files listed for %1").arg(model)}};
        done();
    });
}

void UpdateCenter::checkDriversWin()
{
    QSettings bios("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS", QSettings::NativeFormat);
    const QString model = bios.value("BaseBoardProduct").toString().trimmed();
    if (model.isEmpty()) {
        done();
        return;
    }
    const QString url = QString::fromLatin1(kApiHost) + "GetPDDrivers?website=global&model=" + QUrl::toPercentEncoding(model)
                        + "&cpu=" + QUrl::toPercentEncoding(model) + "&osid=52&systemCode=rog";
    winFetch(url, true, [this](const QJsonArray &obj, const QString &) {
        static const QStringList skip = {"Armoury Crate & Aura Creator Installer", "MyASUS", "ASUS Smart Display Control",
                                         "Aura Wallpaper", "Virtual Pet", "Virtual Pet- Ultimate Edition",
                                         "Armoury Crate Control Interface", "Virtual Assistant"};
        m_drivers.clear();
        const auto newest = newestFiles(obj);
        const QList<Device> devices = newest.isEmpty() ? QList<Device>() : presentDevices();
        static const QRegularExpression revRe("&REV_[0-9A-Fa-f]+", QRegularExpression::CaseInsensitiveOption);
        for (auto it = newest.begin(); it != newest.end(); ++it) {
            const QString cat = it.value().first;
            const QJsonObject file = it.value().second;
            const QString title = file.value("Title").toString();
            if (title.isEmpty() || skip.contains(title, Qt::CaseInsensitive))
                continue;
            QString installed;
            for (const QJsonValue &hv : file.value("HardwareInfoList").toArray()) {
                QString hid = hv.toObject().value("hardwareid").toString().trimmed();
                hid.remove(revRe);
                hid = hid.toLower();
                if (hid.isEmpty())
                    continue;
                for (const Device &d : devices) {
                    for (const QString &id : d.ids)
                        if (id.contains(hid)) {
                            installed = d.driverVersion;
                            break;
                        }
                    if (!installed.isEmpty())
                        break;
                }
                if (!installed.isEmpty())
                    break;
            }
            QVariantMap r = rowFor(title, cat, file, installed, safeName(title));
            r["note"] = cat;
            m_drivers << r;
        }
        done();
    });
}

void UpdateCenter::cancelDownload()
{
    if (m_dlReply)
        m_dlReply->abort();
}

void UpdateCenter::download(const QString &url, const QString &version, const QString &sha256, const QString &folderName)
{
    if (m_installBusy)
        return;
    startDownload(url, version, sha256, folderName, [](bool ok, const QString &path) {
        if (ok)
            showInExplorer(path);
    });
}

void UpdateCenter::startDownload(const QString &url, const QString &version, const QString &sha256, const QString &folderName,
                                 std::function<void(bool, const QString &)> onDone)
{
    if (m_dlReply) {
        onDone(false, QString());
        return;
    }
    auto fail = [this, onDone](const QString &msg) {
        m_downloadStatus = msg;
        emit changed();
        onDone(false, QString());
    };
    const QUrl u(url);
    if (u.scheme() != "https" || u.host().toLower() != "dlcdnets.asus.com") {
        fail(tr("Refused: downloads are only allowed from https://dlcdnets.asus.com."));
        return;
    }
    QString fileName = QFileInfo(u.path()).fileName();
    fileName.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
    if (fileName.isEmpty() || fileName == "." || fileName == "..") {
        fail(tr("The download address has no file name."));
        return;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QDir::homePath() + "/Downloads";
    QString folder = safeName(folderName);
    if (folder.isEmpty())
        folder = "ASUS";
    const QString ver = safeName(version);
    const QString dir = base + "/XPanel-ASUS/" + folder + (ver.isEmpty() ? QString() : "-" + ver);
    if (!QDir().mkpath(dir)) {
        fail(tr("Cannot create %1").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    const QString path = dir + "/" + fileName;
    auto out = std::make_shared<QSaveFile>(path);
    if (!out->open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto writeFailed = std::make_shared<bool>(false);

    QNetworkRequest req{u};
    req.setHeader(QNetworkRequest::UserAgentHeader, "XPanel/" XPANEL_VERSION);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);
    QNetworkReply *reply = m_net.get(req);
    m_dlReply = reply;
    m_downloadProgress = 0;
    m_downloadStatus = tr("Downloading %1…").arg(fileName);
    emit changed();

    auto drain = [reply, out, hash, writeFailed] {
        const QByteArray d = reply->readAll();
        if (d.isEmpty() || *writeFailed)
            return;
        hash->addData(d);
        if (out->write(d) != d.size()) {
            *writeFailed = true;
            reply->abort();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_downloadProgress = total > 0 ? double(got) / double(total) : 0;
        emit changed();
    });
    const QString expected = sha256.trimmed().toLower();
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, hash, writeFailed, drain, expected, path, onDone] {
        reply->deleteLater();
        m_dlReply = nullptr;
        const QNetworkReply::NetworkError err = reply->error();
        const QString errText = reply->errorString();
        if (err == QNetworkReply::NoError)
            drain();
        const QString nativePath = QDir::toNativeSeparators(path);
        bool ok = false;
        if (*writeFailed) {
            out->cancelWriting();
            m_downloadStatus = tr("Cannot write %1").arg(nativePath);
        } else if (err == QNetworkReply::OperationCanceledError) {
            out->cancelWriting();
            m_downloadStatus = tr("Download canceled.");
        } else if (err != QNetworkReply::NoError) {
            out->cancelWriting();
            m_downloadStatus = tr("Download failed: %1").arg(errText);
        } else {
            const QString got = QString::fromLatin1(hash->result().toHex());
            if (!expected.isEmpty() && expected != got) {
                out->cancelWriting();
                m_downloadStatus = tr("Checksum does not match (the file was deleted). Expected %1, got %2.").arg(expected, got);
            } else if (!out->commit()) {
                m_downloadStatus = tr("Cannot write %1").arg(nativePath);
            } else {
                m_downloadProgress = 1;
                m_downloadStatus = expected.isEmpty()
                    ? tr("Saved %1 (no checksum from ASUS). SHA-256: %2. Run the file yourself.").arg(nativePath, got)
                    : tr("Saved %1. SHA-256 matches the ASUS checksum.").arg(nativePath);
                ok = true;
            }
        }
        emit changed();
        onDone(ok, path);
    });
}

// ---------------------------------------------------------------- install

namespace {

QString sysDir(const QString &name)
{
    const QString root = qEnvironmentVariable("SystemRoot", "C:\\Windows");
    return QDir::toNativeSeparators(root + "/System32/" + name);
}

bool isFirmwareRow(const QVariantMap &row)
{
    const QString title = row.value("title").toString();
    return row.value("bios").toBool() || row.value("acPower").toBool() || title.contains("MCU", Qt::CaseInsensitive)
           || title.contains("BIOS", Qt::CaseInsensitive);
}

// the shallowest file whose name matches (case-insensitive); empty if none
QString findShallowest(const QString &root, const QString &name)
{
    QString best;
    int bestDepth = 1 << 30;
    QDirIterator it(root, QStringList{name}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const int depth = int(f.count('/'));
        if (depth < bestDepth) {
            bestDepth = depth;
            best = f;
        }
    }
    return best;
}

// Firmware must not run on battery or with a low battery. Empty string = safe.
QString powerProblem()
{
    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps))
        return UpdateCenter::tr("Cannot read the power state. Connect the charger and try again.");
    if (sps.ACLineStatus != 1)
        return UpdateCenter::tr("Connect the charger first. BIOS and MCU updates need AC power.");
    if (sps.BatteryLifePercent != 255 && sps.BatteryLifePercent < 20)
        return UpdateCenter::tr("The battery is below 20 %. Charge it first.");
    return QString();
}

} // namespace

void UpdateCenter::setInstallStatus(const QString &text)
{
    m_installStatus = text;
    emit changed();
}

// end of one install: show the result, go on with the queue or check again
void UpdateCenter::finishInstall(bool ok, const QString &msg, bool needRestart)
{
    m_installBusy = false;
    if (needRestart)
        m_restartNeeded = true;
    m_installStatus = msg;
    if (!ok && !m_queue.isEmpty()) {
        m_queue.clear();
        m_installStatus += "  " + tr("The other updates were stopped.");
    }
    emit changed();
    if (!ok)
        return;
    if (!m_queue.isEmpty())
        installNext();
    else
        check();
}

void UpdateCenter::installNext()
{
    if (m_queue.isEmpty())
        return;
    const QVariantMap row = m_queue.takeFirst().toMap();
    install(row);
}

void UpdateCenter::installAllDrivers()
{
    if (m_installBusy || m_dlReply)
        return;
    m_queue.clear();
    for (const QVariant &v : std::as_const(m_drivers)) {
        const QVariantMap r = v.toMap();
        if (r.value("status").toString() == "update" && !r.value("url").toString().isEmpty()
            && !r.value("sha256").toString().isEmpty() && !r.value("isStore").toBool() && !isFirmwareRow(r))
            m_queue << r;
    }
    if (m_queue.isEmpty()) {
        setInstallStatus(tr("No driver update to install."));
        return;
    }
    installNext();
}

void UpdateCenter::install(const QVariantMap &row)
{
    if (m_installBusy || m_dlReply)
        return;
    const QString url = row.value("url").toString(), sha = row.value("sha256").toString().trimmed();
    if (row.value("isStore").toBool() || !url.startsWith("https://")) {
        finishInstall(false, tr("This item is not a download. Open it in the Microsoft Store."), false);
        return;
    }
    if (sha.isEmpty()) {
        finishInstall(false, tr("ASUS gave no checksum for this file. XPanel will not install it. Use Download instead."), false);
        return;
    }
    if (isFirmwareRow(row)) {
        const QString problem = powerProblem();
        if (!problem.isEmpty()) {
            finishInstall(false, problem, false);
            return;
        }
    }
    m_installBusy = true;
    m_installStatus = tr("Downloading %1…").arg(row.value("title").toString());
    emit changed();
    startDownload(url, row.value("latest").toString(), sha, row.value("folder").toString(),
                  [this, row](bool ok, const QString &path) {
        if (!ok) {
            finishInstall(false, tr("Download failed. %1").arg(m_downloadStatus), false);
            return;
        }
        m_downloadStatus = tr("Downloaded %1 and checked its SHA-256.").arg(QFileInfo(path).fileName());
        launchDownloaded(path, row);
    });
}

void UpdateCenter::launchDownloaded(const QString &path, const QVariantMap &row)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString exeModule = row.value("exeModule").toString();
    const QString moduleFile = exeModule.section("%%", 0, 0).trimmed();
    const QString moduleSwitches = exeModule.contains("%%") ? exeModule.section("%%", 1).trimmed() : QString();
    if (suffix == "exe") {
        // switches only go to the file that ExeModule names
        const bool named = !moduleFile.isEmpty() && QFileInfo(path).fileName().compare(moduleFile, Qt::CaseInsensitive) == 0;
        runElevated(path, named ? moduleSwitches : QString(), row);
        return;
    }
    if (suffix != "zip") {
        showInExplorer(path);
        finishInstall(false, tr("XPanel does not run %1 files automatically. The folder is open.").arg("." + suffix), false);
        return;
    }
    // unpack next to the zip with the tar.exe that ships with Windows
    const QString dir = QFileInfo(path).absolutePath() + "/extracted";
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    m_installStatus = tr("Unpacking %1…").arg(QFileInfo(path).fileName());
    emit changed();
    auto *tar = new QProcess(this);
    tar->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    connect(tar, &QProcess::finished, this, [this, tar, dir, row, moduleFile, moduleSwitches](int code, QProcess::ExitStatus st) {
        tar->deleteLater();
        if (code != 0 || st != QProcess::NormalExit) {
            finishInstall(false, tr("Cannot unpack the zip file (tar.exe code %1).").arg(code), false);
            return;
        }
        QStringList names;
        if (!moduleFile.isEmpty())
            names << moduleFile;
        names << "AsusSetup.exe" << "setup.exe";
        names.removeDuplicates();
        for (const QString &n : std::as_const(names)) {
            const QString f = findShallowest(dir, n);
            if (!f.isEmpty()) {
                runElevated(f, n.compare(moduleFile, Qt::CaseInsensitive) == 0 ? moduleSwitches : QString(), row);
                return;
            }
        }
        QDirIterator inf(dir, QStringList{"*.inf"}, QDir::Files, QDirIterator::Subdirectories);
        if (inf.hasNext()) {
            runElevated(sysDir("pnputil.exe"),
                        "/add-driver \"" + QDir::toNativeSeparators(dir) + "\\*.inf\" /subdirs /install", row);
            return;
        }
        showInExplorer(dir);
        finishInstall(false, tr("The zip file has no installer XPanel can run. The folder is open."), false);
    });
    tar->start(sysDir("tar.exe"), {"-xf", QDir::toNativeSeparators(path), "-C", QDir::toNativeSeparators(dir)});
}

// start a program with the UAC prompt, then poll until it ends (no UI freeze)
void UpdateCenter::runElevated(const QString &file, const QString &params, const QVariantMap &row)
{
    const std::wstring wfile = QDir::toNativeSeparators(file).toStdWString();
    const std::wstring wparams = params.toStdWString();
    const std::wstring wdir = QDir::toNativeSeparators(QFileInfo(file).absolutePath()).toStdWString();
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = wfile.c_str();
    sei.lpParameters = wparams.empty() ? nullptr : wparams.c_str();
    sei.lpDirectory = wdir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    m_installStatus = tr("Installing %1… Do not turn off the device.").arg(row.value("title").toString());
    emit changed();
    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        finishInstall(false, err == ERROR_CANCELLED ? tr("Cancelled.") : tr("Cannot start the installer (Windows error %1).").arg(err), false);
        return;
    }
    const bool rebootRow = row.value("reboot").toString().trimmed() == "1";
    if (!sei.hProcess) {
        finishInstall(true, tr("Installer started."), rebootRow);
        return;
    }
    HANDLE h = sei.hProcess;
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, [this, timer, h, rebootRow, row] {
        if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0)
            return;
        DWORD code = 1;
        GetExitCodeProcess(h, &code);
        CloseHandle(h);
        timer->stop();
        timer->deleteLater();
        const QString title = row.value("title").toString();
        if (code == 0 || code == 3010 || code == 1641) {
            const bool restart = code != 0 || rebootRow;
            finishInstall(true, restart ? tr("%1 installed. Restart the device to finish.").arg(title)
                                        : tr("%1 installed.").arg(title), restart);
        } else {
            finishInstall(false, tr("%1: the installer ended with code %2.").arg(title).arg(code), false);
        }
    });
    timer->start();
}

void UpdateCenter::restartNow()
{
    QProcess::startDetached("shutdown.exe", {"/r", "/t", "5"});
}

void UpdateCenter::openStore(const QString &productId)
{
    static const QRegularExpression valid("^[0-9A-Za-z]{6,20}$");
    if (valid.match(productId).hasMatch())
        QDesktopServices::openUrl(QUrl("ms-windows-store://pdp/?ProductId=" + productId));
}

#endif // Q_OS_WIN
