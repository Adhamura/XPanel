import QtQuick
import "../Theme.js" as T

// Settings > Audio: speaker / microphone choice, microphone level, AI noise cancelling.
CcPage {
    id: page
    Component.onCompleted: audio.refresh()
    readonly property var mic: audio.inputs.find(i => i.name === audio.defaultInput) || null

    CcSection { title: qsTr("Speaker") }
    CcRow { title: qsTr("Volume")
        CcSlider { width: 460; from: 0; to: 100; unit: " %"; value: sys.volume; onMoved: (v) => sys.setVolume(v) } }
    Repeater {
        model: audio.outputs
        CcRow { title: modelData.label; description: modelData.name === audio.defaultOutput ? qsTr("In use") : ""
            CcButton { text: modelData.name === audio.defaultOutput ? qsTr("Selected") : qsTr("Use"); primary: modelData.name === audio.defaultOutput
                       onClicked: audio.setDefaultOutput(modelData.name) } }
    }

    CcSection { title: qsTr("Microphone") }
    Repeater {
        model: audio.inputs
        CcRow { title: modelData.label; description: modelData.name === audio.defaultInput ? qsTr("In use") : ""
            CcButton { text: modelData.name === audio.defaultInput ? qsTr("Selected") : qsTr("Use"); primary: modelData.name === audio.defaultInput
                       onClicked: audio.setDefaultInput(modelData.name) } }
    }
    CcRow { title: qsTr("Microphone level"); visible: !!page.mic
        CcSlider { width: 460; from: 0; to: desktop.windows ? 100 : 150; unit: " %"; value: page.mic ? page.mic.volume : 0
                   onMoved: (v) => audio.setInputVolume(audio.defaultInput, v) } }
    CcRow { title: qsTr("Mute microphone"); visible: !!page.mic
        CcSwitch { checked: page.mic ? page.mic.muted : false; onToggled: (v) => audio.setInputMute(audio.defaultInput, v) } }

    CcSection { title: qsTr("AI Noise-Canceling"); visible: !desktop.windows }
    CcRow { title: qsTr("AI Noise-Canceling microphone"); visible: !desktop.windows
            description: audio.noiseCancelAvailable ? qsTr("Removes background noise from your voice (RNNoise).")
                         : qsTr("Needs the RNNoise plugin: sudo pacman -S noise-suppression-for-voice")
        CcSwitch { enabledSwitch: false; checked: false } }
}
