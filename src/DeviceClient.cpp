#include "DeviceClient.h"

#include <QCoreApplication>
#include <QJsonDocument>

static QString socketPath()
{
#ifdef Q_OS_WIN
    return qEnvironmentVariable("XPANEL_SOCKET", "xpanel"); // named pipe of xpanel-daemon.exe
#else
    return qEnvironmentVariable("XPANEL_SOCKET", "/run/xpanel/xpanel.sock");
#endif
}

DeviceClient::DeviceClient(QObject *parent) : QObject(parent)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(3000);
    connect(&m_retry, &QTimer::timeout, this, &DeviceClient::connectNow);
    connect(&m_sock, &QLocalSocket::connected, this, [this] {
        send({{"get", true}});
        if (m_grab) send({{"input", QJsonObject{{"grab", true}}}});
    });
    connect(&m_sock, &QLocalSocket::disconnected, this, [this] {
        if (m_ready) {
            m_ready = false;
            emit connectedChanged();
        }
        m_retry.start();
    });
    connect(&m_sock, &QLocalSocket::errorOccurred, this, [this] {
        if (m_sock.state() != QLocalSocket::ConnectedState)
            m_retry.start();
    });
    connect(&m_sock, &QLocalSocket::readyRead, this, &DeviceClient::onRead);
    connectNow();
}

QString DeviceClient::installHint()
{
#ifdef Q_OS_WIN
    return tr("restart XPanel (it starts xpanel-daemon.exe), or reinstall it");
#else
    if (!qEnvironmentVariableIsEmpty("APPIMAGE"))
        return "sudo " + qEnvironmentVariable("APPIMAGE") + " --install";
    if (QCoreApplication::applicationDirPath().startsWith("/opt/xpanel/"))
        return "sudo xpanel --install";
    return "sudo scripts/install-daemon.sh";
#endif
}

void DeviceClient::connectNow()
{
    if (m_sock.state() != QLocalSocket::UnconnectedState)
        m_sock.abort();
    m_sock.connectToServer(socketPath());
}

void DeviceClient::onRead()
{
    while (m_sock.canReadLine()) {
        const QJsonObject msg = QJsonDocument::fromJson(m_sock.readLine()).object();
        if (msg.contains("event")) {
            const QString kind = msg["event"].toString(), name = msg["name"].toString();
            if (kind == "special") emit special(name);
            else if (kind == "nav") emit nav(name);
            else if (kind == "action") emit action(name);
            continue;
        }
        if (msg.contains("error")) {
            const QString why = msg["message"].toString(msg["error"].toString());
            if (why != m_error) {
                qWarning("XPanel: xpanel-daemon refused the connection: %s", qPrintable(why));
                m_error = why;
                emit errorChanged();
            }
            continue;
        }
        if (msg.contains("state")) {
            m_state = msg["state"].toObject();
            ++m_rev;
            emit stateChanged();
            if (!m_ready) {
                m_ready = true;
                emit connectedChanged();
                if (!m_error.isEmpty()) {
                    m_error.clear();
                    emit errorChanged();
                }
            }
        }
    }
}

void DeviceClient::send(const QJsonObject &msg)
{
    if (m_sock.state() != QLocalSocket::ConnectedState)
        return;
    m_sock.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n");
    m_sock.flush();
}

void DeviceClient::setGrab(bool grab)
{
    if (grab == m_grab)
        return;
    m_grab = grab;
    send({{"input", QJsonObject{{"grab", grab}}}});
}

void DeviceClient::calibrateGyro()
{
    send({{"input", QJsonObject{{"calibrate", "gyro"}}}});
}

QVariant DeviceClient::value(const QString &key) const
{
    return m_state.value(key).toVariant();
}

void DeviceClient::setValue(const QString &key, const QVariant &value)
{
    // Show the new value at once; the daemon confirms with the next state push.
    m_state[key] = QJsonValue::fromVariant(value);
    ++m_rev;
    emit stateChanged();
    send({{"set", QJsonObject{{key, QJsonValue::fromVariant(value)}}}});
}
