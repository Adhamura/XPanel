#include "NetworkControl.h"
#include "Proc.h"

#include <QRegularExpression>
#include <QSet>

NetworkControl::NetworkControl(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    winInit();
#endif
}

void NetworkControl::begin() { ++m_busy; emit busyChanged(); }
void NetworkControl::end() { m_busy = qMax(0, m_busy - 1); emit busyChanged(); }
void NetworkControl::setMessage(const QString &m) { m_message = m; emit messageChanged(); }

#ifndef Q_OS_WIN // Windows: src/NetworkControlWin.cpp
// nmcli -t escapes ':' inside values as "\:"
static QStringList splitTerse(const QString &line)
{
    QStringList out;
    QString cur;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) { cur += line[++i]; continue; }
        if (line[i] == ':') { out << cur; cur.clear(); continue; }
        cur += line[i];
    }
    out << cur;
    return out;
}

void NetworkControl::scanWifi()
{
    begin();
    runProcess(this, "nmcli", {"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "yes"},
               [this](const QString &out, int) {
        QVariantList list;
        QSet<QString> seen;
        for (const QString &l : out.split('\n', Qt::SkipEmptyParts)) {
            const QStringList f = splitTerse(l);
            if (f.size() < 4 || f[1].isEmpty() || seen.contains(f[1]))
                continue;
            seen.insert(f[1]);
            list << QVariantMap{{"active", f[0] == "*"}, {"ssid", f[1]}, {"signal", f[2].toInt()},
                                {"secure", !f[3].isEmpty() && f[3] != "--"}, {"security", f[3]}};
        }
        m_networks = list;
        emit networksChanged();
        end();
    });
}

void NetworkControl::connectWifi(const QString &ssid, const QString &password)
{
    begin();
    setMessage(tr("Connecting to %1…").arg(ssid));
    QStringList args{"device", "wifi", "connect", ssid};
    if (!password.isEmpty())
        args << "password" << password;
    runProcess(this, "nmcli", args, [this, ssid](const QString &, int code) {
        setMessage(code == 0 ? tr("Connected to %1").arg(ssid) : tr("Could not connect to %1").arg(ssid));
        end();
        scanWifi();
    });
}

void NetworkControl::disconnectWifi()
{
    begin();
    runProcess(this, "sh", {"-c", "nmcli -t -f DEVICE,TYPE device | grep ':wifi$' | cut -d: -f1 | xargs -r -n1 nmcli device disconnect"},
               [this](const QString &, int) { end(); scanWifi(); });
}

void NetworkControl::listBluetooth()
{
    // "Device AA:BB:.. Name" for every known device; then ask which are connected / paired
    runProcess(this, "bluetoothctl", {"devices"}, [this](const QString &all, int) {
        runProcess(this, "bluetoothctl", {"devices", "Connected"}, [this, all](const QString &conn, int) {
            runProcess(this, "bluetoothctl", {"devices", "Paired"}, [this, all, conn](const QString &paired, int) {
                QVariantList list;
                static const QRegularExpression re("^Device ([0-9A-F:]{17}) (.*)$");
                for (const QString &l : all.split('\n', Qt::SkipEmptyParts)) {
                    const auto m = re.match(l.trimmed());
                    if (!m.hasMatch()) continue;
                    const QString mac = m.captured(1);
                    list << QVariantMap{{"mac", mac}, {"name", m.captured(2)},
                                        {"connected", conn.contains(mac)}, {"paired", paired.contains(mac)}};
                }
                std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
                    return a.toMap()["connected"].toBool() > b.toMap()["connected"].toBool()
                        || (a.toMap()["connected"] == b.toMap()["connected"]
                            && a.toMap()["paired"].toBool() > b.toMap()["paired"].toBool());
                });
                m_devices = list;
                emit devicesChanged();
            });
        });
    });
}

void NetworkControl::scanBluetooth()
{
    begin();
    listBluetooth();
    // discover new devices for 8 seconds
    runProcess(this, "bluetoothctl", {"--timeout", "8", "scan", "on"}, [this](const QString &, int) {
        listBluetooth();
        end();
    }, 15000);
}

void NetworkControl::connectBluetooth(const QString &mac)
{
    // the address goes into a shell command: accept only a real MAC
    static const QRegularExpression macRe("^[0-9A-F]{2}(:[0-9A-F]{2}){5}$");
    if (!macRe.match(mac).hasMatch())
        return;
    begin();
    setMessage(tr("Connecting…"));
    // pair + trust first for new devices; harmless for known ones
    runProcess(this, "sh", {"-c", QString("bluetoothctl pair %1; bluetoothctl trust %1; bluetoothctl connect %1").arg(mac)},
               [this](const QString &out, int) {
        setMessage(out.contains("Connection successful") ? tr("Connected") : tr("Could not connect"));
        listBluetooth();
        end();
    }, 30000);
}

void NetworkControl::disconnectBluetooth(const QString &mac)
{
    runProcess(this, "bluetoothctl", {"disconnect", mac}, [this](const QString &, int) { listBluetooth(); });
}

void NetworkControl::removeBluetooth(const QString &mac)
{
    runProcess(this, "bluetoothctl", {"remove", mac}, [this](const QString &, int) { listBluetooth(); });
}
#endif // !Q_OS_WIN
