#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

static const QStringList kDefaultTiles = {
    "opmode", "ctrlmode", "fan",
    "keyboard", "monitor", "charge",
    "desktop", "rgb", "refresh",
};

Config::Config(QObject *parent) : QObject(parent)
{
    load();
}

QString Config::path() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/XPanel/config.json";
}

void Config::load()
{
    m_tiles = kDefaultTiles;
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.contains("tiles")) {
        m_tiles.clear();
        for (const QJsonValue &v : o.value("tiles").toArray()) {
            const QString id = v.toString();
            if (!m_tiles.contains(id))
                m_tiles << id;
        }
    }
    m_monitorOsd = o.value("monitorOsd").toBool(false);
    m_button = o.value("button").toString("left_button");
    m_values = o.value("values").toObject().toVariantMap();
}

void Config::save() const
{
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QFile f(path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonObject o;
    o.insert("tiles", QJsonArray::fromStringList(m_tiles));
    o.insert("monitorOsd", m_monitorOsd);
    o.insert("button", m_button);
    o.insert("values", QJsonObject::fromVariantMap(m_values));
    f.write(QJsonDocument(o).toJson());
}

void Config::setTiles(const QStringList &t)
{
    if (t == m_tiles)
        return;
    m_tiles = t;
    save();
    emit tilesChanged();
}

void Config::setMonitorOsd(bool on)
{
    if (on == m_monitorOsd)
        return;
    m_monitorOsd = on;
    save();
    emit monitorOsdChanged();
}

void Config::setButton(const QString &b)
{
    if (b == m_button)
        return;
    m_button = b;
    save();
    emit buttonChanged();
}

QVariant Config::get(const QString &key, const QVariant &fallback) const
{
    return m_values.value(key, fallback);
}

void Config::set(const QString &key, const QVariant &value)
{
    // an empty value (undefined / null from QML) removes the key: the default applies again
    if (!value.isValid() || value.isNull()) {
        if (!m_values.remove(key))
            return;
        ++m_rev;
        save();
        emit valuesChanged();
        return;
    }
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    ++m_rev;
    save();
    emit valuesChanged();
}

QVariantMap Config::group(const QString &prefix) const
{
    QVariantMap out;
    const QString p = prefix + ".";
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        if (it.key().startsWith(p))
            out.insert(it.key().mid(p.size()), it.value());
    return out;
}

void Config::toggleTile(const QString &id)
{
    QStringList t = m_tiles;
    if (t.contains(id))
        t.removeAll(id);
    else
        t << id;
    setTiles(t);
}

void Config::moveTile(const QString &id, int delta)
{
    QStringList t = m_tiles;
    const int i = t.indexOf(id);
    const int j = i + delta;
    if (i < 0 || j < 0 || j >= t.size())
        return;
    t.move(i, j);
    setTiles(t);
}

void Config::resetTiles()
{
    setTiles(kDefaultTiles);
}
