#include "UpdateCenter.h"
#include "Desktop.h"
#include "Proc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

// Packages that carry the Ally X drivers and firmware on Linux.
static const QList<QPair<QString, QString>> kDriverPackages = {
    {"^linux(-cachyos[a-z-]*)?$", "Kernel: hid_asus_ally, asus-armoury, amdgpu, mt7921 drivers"},
    {"^linux(-cachyos[a-z-]*)?-headers$", "Kernel headers (for DKMS modules)"},
    {"^linux-firmware(-[a-z]+)?$", "Firmware files: AMD GPU, MediaTek Wi-Fi / Bluetooth, audio"},
    {"^amd-ucode$", "AMD CPU microcode"},
    {"^(lib32-)?mesa$", "Graphics driver (OpenGL)"},
    {"^(lib32-)?vulkan-radeon$", "Graphics driver (Vulkan, RADV)"},
    {"^sof-firmware$", "Audio DSP firmware"},
    {"^alsa-ucm-conf$", "Audio device profiles"},
    {"^bluez$", "Bluetooth stack"},
    {"^networkmanager$", "Wi-Fi / network"},
    {"^fwupd$", "Firmware updater"},
};

void UpdateCenter::done()
{
    if (--m_pending > 0)
        return;
    m_packages = m_collect;
    // drivers & firmware packages: installed version + pending update
    QMap<QString, QString> pending;
    for (const QVariant &v : std::as_const(m_collect))
        pending[v.toMap()["name"].toString()] = v.toMap()["to"].toString();
    m_drivers.clear();
    for (auto it = m_installed.begin(); it != m_installed.end(); ++it) {
        for (const auto &d : kDriverPackages) {
            if (!QRegularExpression(d.first).match(it.key()).hasMatch())
                continue;
            m_drivers << QVariantMap{{"name", it.key()}, {"installed", it.value()},
                                     {"update", pending.value(it.key())}, {"note", d.second}};
            break;
        }
    }
    m_lastCheck = QDateTime::currentDateTime().toString("dd.MM.yyyy HH:mm");
    emit changed();
}

void UpdateCenter::check()
{
    if (m_pending > 0)
        return;
    m_collect.clear();
    m_firmware.clear();
    m_pending = 5;
    emit changed();
    checkDrivers();
    checkFwDevices(); // then the ASUS list (needs the installed versions)
    // "name old -> new"
    auto addLines = [this](const QString &out, const QString &source) {
        for (const QString &l : out.split('\n', Qt::SkipEmptyParts)) {
            const QStringList f = l.split(' ', Qt::SkipEmptyParts);
            if (f.size() >= 4)
                m_collect << QVariantMap{{"name", f[0]}, {"from", f[1]}, {"to", f[3]}, {"source", source}};
        }
    };
    runProcess(this, "checkupdates", {}, [this, addLines](const QString &out, int) {
        addLines(out, "repo");
        done();
    }, 180000);
    if (!QStandardPaths::findExecutable("paru").isEmpty())
        runProcess(this, "paru", {"-Qua"}, [this, addLines](const QString &out, int) {
            addLines(out, "AUR");
            done();
        }, 180000);
    else
        done();
    runProcess(this, "fwupdmgr", {"get-updates", "--json", "--no-unreported-check", "--no-metadata-check"},
               [this](const QString &out, int) {
        for (const QJsonValue &d : QJsonDocument::fromJson(out.toUtf8()).object()["Devices"].toArray()) {
            const QJsonObject o = d.toObject();
            const QJsonArray rel = o["Releases"].toArray();
            if (rel.isEmpty()) continue;
            m_firmware << QVariantMap{{"name", o["Name"].toString()}, {"from", o["Version"].toString()},
                                      {"to", rel.first().toObject()["Version"].toString()}};
        }
        done();
    }, 180000);
}

void UpdateCenter::checkDrivers()
{
    runProcess(this, "pacman", {"-Q"}, [this](const QString &out, int) {
        m_installed.clear();
        for (const QString &l : out.split('\n', Qt::SkipEmptyParts))
            m_installed[l.section(' ', 0, 0)] = l.section(' ', 1, 1);
        done();
    });
}

void UpdateCenter::checkFwDevices()
{
    runProcess(this, "fwupdmgr", {"get-devices", "--json"}, [this](const QString &out, int) {
        m_fwDevices.clear();
        m_fwVersions.clear();
        for (const QJsonValue &d : QJsonDocument::fromJson(out.toUtf8()).object()["Devices"].toArray()) {
            const QJsonObject o = d.toObject();
            const QString name = o["Name"].toString(), plugin = o["Plugin"].toString();
            QString ver = o["Version"].toString();
            if (name.isEmpty() || ver.isEmpty())
                continue;
            if (plugin == "uefi_capsule" && name == "System Firmware") {
                bool ok = false;
                const int n = ver.toInt(&ok);
                if (ok) ver = QString::number(n, 16); // 786 = 0x312 = BIOS 312
                m_fwVersions["bios"] = ver;
            }
            if (plugin == "asus_hid" && name.contains("Microcontroller"))
                m_fwVersions["mcu"] = ver;
            const QStringList flags = o["Flags"].toVariant().toStringList();
            m_fwDevices << QVariantMap{{"name", name}, {"vendor", o["Vendor"].toString()}, {"version", ver},
                                       {"updatable", flags.contains("updatable")}, {"plugin", plugin}};
        }
        checkAsus();
    });
}

// ASUS support site: BIOS and firmware files for this model.
void UpdateCenter::checkAsus()
{
    QFile f("/sys/class/dmi/id/board_name");
    const QString model = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    QFile b("/sys/class/dmi/id/bios_version");
    const QString biosNow = b.open(QIODevice::ReadOnly) ? QString::fromUtf8(b.readAll()).trimmed().section('.', -1) : m_fwVersions.value("bios");
    QNetworkRequest req(QUrl("https://www.asus.com/support/api/product.asmx/GetPDBIOS?website=global&model=" + model));
    req.setHeader(QNetworkRequest::UserAgentHeader, "XPanel/" XPANEL_VERSION);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, biosNow, model] {
        reply->deleteLater();
        m_allyFw.clear();
        const QJsonObject res = QJsonDocument::fromJson(reply->readAll()).object()["Result"].toObject();
        // newest file per title in each category
        QMap<QString, QJsonObject> newest;
        for (const QJsonValue &c : res["Obj"].toArray()) {
            const QString cat = c.toObject()["Name"].toString();
            if (cat.contains("Windows"))
                continue; // the same BIOS packed as a Windows program
            for (const QJsonValue &fv : c.toObject()["Files"].toArray()) {
                const QJsonObject file = fv.toObject();
                const QString key = cat + "|" + file["Title"].toString();
                auto num = [](const QString &v) { return QString(v).remove(QRegularExpression("[^0-9]")).toInt(); };
                if (!newest.contains(key) || num(file["Version"].toString()) > num(newest[key]["Version"].toString()))
                    newest[key] = file;
            }
        }
        for (auto it = newest.begin(); it != newest.end(); ++it) {
            const QJsonObject file = it.value();
            const QString title = file["Title"].toString(), latest = file["Version"].toString();
            QString installed, name = title, how;
            if (it.key().startsWith("BIOS|")) {
                name = "BIOS"; installed = biosNow;
                how = "Linux: download, put the .CAP file on a FAT32 USB stick, restart to BIOS, use EZ Flash.";
            } else if (title.contains("MCU", Qt::CaseInsensitive)) {
                name = "Controller (MCU)"; installed = m_fwVersions.value("mcu");
                how = "ASUS ships it only as a Windows tool. Linux needs version 313 or newer.";
            } else {
                how = "ASUS ships it only as a Windows tool.";
            }
            auto num = [](const QString &v) { return QString(v).remove(QRegularExpression("[^0-9]")).toInt(); };
            QString status = "unknown";
            if (!installed.isEmpty())
                status = num(latest) > num(installed) ? "update" : (num(latest) < num(installed) ? "newer" : "current");
            m_allyFw << QVariantMap{{"name", name}, {"title", title}, {"installed", installed}, {"latest", latest},
                                    {"date", file["ReleaseDate"].toString()}, {"size", file["FileSize"].toString()},
                                    {"url", file["DownloadUrl"].toObject()["Global"].toString()},
                                    {"status", status}, {"how", how}, {"bios", name == "BIOS"}};
        }
        if (m_allyFw.isEmpty())
            m_allyFw << QVariantMap{{"name", "ASUS support site"}, {"title", model}, {"status", "unknown"},
                                    {"how", reply->error() ? "Not reachable: " + reply->errorString() : "No files listed for " + model}};
        done();
    });
}

void UpdateCenter::refreshFirmwareMetadata()
{
    Desktop::runInTerminal({"fwupdmgr", "refresh", "--force"});
}

void UpdateCenter::downloadBios(const QString &url, const QString &version)
{
    // only ASUS's own download server
    if (!url.startsWith("https://dlcdnets.asus.com/"))
        return;
    const QString dir = QDir::homePath() + "/Downloads/XPanel-BIOS-" + QString(version).remove(QRegularExpression("[^0-9A-Za-z]"));
    QDir().mkpath(dir);
    const QString file = dir + "/" + QUrl(url).fileName();
    m_downloadStatus = "Downloading " + QUrl(url).fileName() + "…";
    emit changed();
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "XPanel/" XPANEL_VERSION);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, dir] {
        reply->deleteLater();
        if (reply->error()) {
            m_downloadStatus = "Download failed: " + reply->errorString();
            emit changed();
            return;
        }
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly)) { m_downloadStatus = "Cannot write " + file; emit changed(); return; }
        out.write(reply->readAll());
        out.commit();
        // the EZ Flash zip holds the .CAP capsule
        runProcess(this, "bsdtar", {"-xf", file, "-C", dir}, [this, dir](const QString &, int code) {
            m_downloadStatus = code == 0 ? "Saved in " + dir + ". Copy the .CAP file to a FAT32 USB stick, then Restart to BIOS → EZ Flash."
                                         : "Saved in " + dir + " (unpack the zip yourself).";
            emit changed();
            Desktop::showInFileManager(dir, false);
        });
    });
}

void UpdateCenter::restartToFirmware()
{
    QProcess::startDetached("systemctl", {"reboot", "--firmware-setup"});
}

void UpdateCenter::openUrl(const QString &url)
{
    if (url.startsWith("https://"))
        QProcess::startDetached("xdg-open", {url});
}

void UpdateCenter::updatePackages()
{
    const QString tool = QStandardPaths::findExecutable("paru").isEmpty() ? "sudo pacman -Syu" : "paru -Syu";
    Desktop::runInTerminal({"sh", "-c", tool});
}

void UpdateCenter::updateFirmware()
{
    Desktop::runInTerminal({"fwupdmgr", "update"});
}
