#include "DeviceClient.h"

#include <QJsonDocument>

static QString socketPath()
{
    return qEnvironmentVariable("XPANEL_SOCKET", "/run/xpanel/xpanel.sock");
}

DeviceClient::DeviceClient(QObject *parent) : QObject(parent)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(3000);
    connect(&m_retry, &QTimer::timeout, this, &DeviceClient::connectNow);
    connect(&m_sock, &QLocalSocket::connected, this, [this] {
        emit connectedChanged();
        send({{"get", true}});
        if (m_grab) send({{"input", QJsonObject{{"grab", true}}}});
    });
    connect(&m_sock, &QLocalSocket::disconnected, this, [this] {
        emit connectedChanged();
        m_retry.start();
    });
    connect(&m_sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError e) {
        static bool told = false;
        if (e == QLocalSocket::SocketAccessError && !told) {
            told = true;
            qWarning("XPanel: no access to %s. Add your user to group xpanel "
                     "(sudo usermod -aG xpanel $USER), then log out and in again.", qPrintable(socketPath()));
        }
        if (m_sock.state() != QLocalSocket::ConnectedState)
            m_retry.start();
    });
    connect(&m_sock, &QLocalSocket::readyRead, this, &DeviceClient::onRead);
    connectNow();
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
        if (msg.contains("state")) {
            m_state = msg["state"].toObject();
            ++m_rev;
            emit stateChanged();
        }
    }
}

void DeviceClient::send(const QJsonObject &msg)
{
    if (!connected())
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
