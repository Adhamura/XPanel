#include "ProfileManager.h"
#include "Config.h"
#include "DeviceClient.h"
#include "GameLibrary.h"
#include "GameWatcher.h"
#include "ProfileRules.h"

static const char *kRecord = "profiles.restore";

ProfileManager::ProfileManager(Config *config, DeviceClient *device, GameWatcher *watcher,
                               GameLibrary *library, QObject *parent)
    : QObject(parent), m_config(config), m_device(device), m_watcher(watcher), m_library(library)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(0);
    connect(&m_syncTimer, &QTimer::timeout, this, &ProfileManager::sync);
    m_launchTimer.setSingleShot(true);
    m_launchTimer.setInterval(20000);
    connect(&m_launchTimer, &QTimer::timeout, this, [this] {
        m_launch.clear(); // never showed up (failed to start, or not traceable)
        m_syncTimer.start();
    });
    auto later = [this] { m_syncTimer.start(); };
    connect(m_watcher, &GameWatcher::currentChanged, this, later);
    connect(m_watcher, &GameWatcher::gameEnded, this, [this](const QString &id) {
        if (id == m_forced) m_forced.clear();
        m_syncTimer.start();
    });
    connect(m_device, &DeviceClient::connectedChanged, this, later);
    connect(m_config, &Config::valuesChanged, this, later); // a profile turned on while its game runs
    m_syncTimer.start();
}

void ProfileManager::play(const QString &id)
{
    if (id.startsWith("app:")) {
        m_launch = id;
        m_launchTitle = m_library->titleOf(id);
        if (m_launchTitle.isEmpty()) m_launchTitle = id.mid(4);
        m_launchTimer.start();
    } else {
        m_forced = id;
    }
    m_library->launch(id);
    m_syncTimer.start();
}

bool ProfileManager::applies(const QString &id) const
{
    if (id.isEmpty())
        return false;
    const QVariantMap p = m_config->get("game." + id).toMap();
    if (!ProfileRules::hasSettings(p))
        return false;
    // desktop games are only seen when started from the library
    return p.value("enabled", true).toBool() || id.startsWith("app:") || id == m_forced;
}

QVariant ProfileManager::current(const QString &key) const
{
    if (key == "preset")
        return m_config->get("control.preset", "gamepad");
    return m_device->value(key);
}

void ProfileManager::set(const QString &key, const QVariant &value)
{
    if (key == "preset")
        m_config->set("control.preset", value); // Main.qml sends the resolved preset to the daemon
    else
        m_device->setValue(key, value);
}

void ProfileManager::sync()
{
    // the game: the newest running one, or a desktop game just started from the library
    if (!m_launch.isEmpty() && m_watcher->isRunning(m_launch)) {
        m_launch.clear();
        m_launchTimer.stop();
    }
    QString game = m_watcher->currentGame(), title = m_watcher->currentTitle();
    if (!m_launch.isEmpty()) {
        game = m_launch;
        title = m_launchTitle;
    }
    const QString want = applies(game) ? game : QString();

    const QVariantMap record = m_config->get(kRecord).toMap();
    const QString had = record.value("game").toString();
    bool active = false;
    if (!had.isEmpty() && had == want) {
        active = true; // already applied (also after a restart of XPanel)
    } else if (m_device->connected()) { // else: wait, the daemon would not get the values
        if (!record.isEmpty()) {
            restore(record);
            m_config->set(kRecord, QVariant());
        }
        if (!want.isEmpty()) {
            apply(want, title);
            active = true;
        }
    }
    if (game != m_game || title != m_title || active != m_active) {
        m_game = game;
        m_title = title;
        m_active = active;
        emit activeChanged();
    }
}

void ProfileManager::apply(const QString &id, const QString &title)
{
    const QVariantMap settings = ProfileRules::profileSettings(m_config->get("game." + id).toMap());
    QVariantMap snapshot;
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        const QVariant v = current(it.key());
        if (v.isValid() && !v.isNull())
            snapshot.insert(it.key(), v);
    }
    // saved first: a crash from here on still restores
    m_config->set(kRecord, QVariantMap{{"game", id}, {"title", title}, {"snapshot", snapshot}, {"applied", settings}});
    for (auto it = settings.begin(); it != settings.end(); ++it)
        set(it.key(), it.value());
    qInfo("XPanel: applied the profile of %s", qPrintable(title));
}

void ProfileManager::restore(const QVariantMap &record)
{
    const QVariantMap applied = record.value("applied").toMap();
    QVariantMap now;
    for (auto it = applied.begin(); it != applied.end(); ++it) {
        const QVariant v = current(it.key());
        if (v.isValid() && !v.isNull())
            now.insert(it.key(), v);
    }
    const QVariantMap back = ProfileRules::restoreChanges(record.value("snapshot").toMap(), applied, now);
    for (auto it = back.begin(); it != back.end(); ++it)
        set(it.key(), it.value());
    qInfo("XPanel: restored the settings from before %s", qPrintable(record.value("title").toString()));
}
