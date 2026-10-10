#include "AudioControl.h"
#include "Proc.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

AudioControl::AudioControl(QObject *parent) : QObject(parent)
{
    refresh();
}

bool AudioControl::noiseCancelAvailable() const
{
    // the RNNoise LADSPA plugin (package noise-suppression-for-voice)
    return QFile::exists("/usr/lib/ladspa/librnnoise_ladspa.so");
}

QVariantList AudioControl::parse(const QString &json, bool inputs) const
{
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        const QString name = o["name"].toString();
        if (inputs && name.endsWith(".monitor"))
            continue; // monitors of outputs are not microphones
        // average of the channel volumes, in percent
        int vol = 0, n = 0;
        const QJsonObject chans = o["volume"].toObject();
        for (const QJsonValue &c : chans) {
            vol += c.toObject()["value_percent"].toString().chopped(1).toInt();
            ++n;
        }
        out << QVariantMap{{"name", name}, {"label", o["description"].toString()},
                           {"volume", n ? vol / n : 0}, {"muted", o["mute"].toBool()}};
    }
    return out;
}

void AudioControl::refresh()
{
    runProcess(this, "pactl", {"--format=json", "list", "sinks"}, [this](const QString &out, int) {
        m_outputs = parse(out, false);
        emit changed();
    });
    runProcess(this, "pactl", {"--format=json", "list", "sources"}, [this](const QString &out, int) {
        m_inputs = parse(out, true);
        emit changed();
    });
    runProcess(this, "pactl", {"get-default-sink"}, [this](const QString &out, int) {
        m_defOut = out.trimmed();
        emit changed();
    });
    runProcess(this, "pactl", {"get-default-source"}, [this](const QString &out, int) {
        m_defIn = out.trimmed();
        emit changed();
    });
}

void AudioControl::setDefaultOutput(const QString &name)
{
    runProcess(this, "pactl", {"set-default-sink", name}, [this](const QString &, int) { refresh(); });
}

void AudioControl::setDefaultInput(const QString &name)
{
    runProcess(this, "pactl", {"set-default-source", name}, [this](const QString &, int) { refresh(); });
}

void AudioControl::setInputVolume(const QString &name, int percent)
{
    runProcess(this, "pactl", {"set-source-volume", name, QString::number(qBound(0, percent, 150)) + "%"});
}

void AudioControl::setInputMute(const QString &name, bool mute)
{
    runProcess(this, "pactl", {"set-source-mute", name, mute ? "1" : "0"}, [this](const QString &, int) { refresh(); });
}
