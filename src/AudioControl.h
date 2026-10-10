#pragma once

#include <QObject>
#include <QVariantList>

// Settings > Audio: output / input device, microphone volume and mute (PipeWire via pactl).
class AudioControl : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
    Q_PROPERTY(QString defaultOutput READ defaultOutput NOTIFY changed)
    Q_PROPERTY(QString defaultInput READ defaultInput NOTIFY changed)
    Q_PROPERTY(bool noiseCancelAvailable READ noiseCancelAvailable CONSTANT)

public:
    explicit AudioControl(QObject *parent = nullptr);
    QVariantList outputs() const { return m_outputs; }
    QVariantList inputs() const { return m_inputs; }
    QString defaultOutput() const { return m_defOut; }
    QString defaultInput() const { return m_defIn; }
    bool noiseCancelAvailable() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setDefaultOutput(const QString &name);
    Q_INVOKABLE void setDefaultInput(const QString &name);
    Q_INVOKABLE void setInputVolume(const QString &name, int percent);
    Q_INVOKABLE void setInputMute(const QString &name, bool mute);

signals:
    void changed();

private:
    QVariantList parse(const QString &json, bool inputs) const;
    QVariantList m_outputs, m_inputs;
    QString m_defOut, m_defIn;
};
