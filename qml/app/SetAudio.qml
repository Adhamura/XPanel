import QtQuick
import "../Theme.js" as T

// Settings > Audio: speaker / microphone choice, microphone level, AI noise cancelling.
CcPage {
    id: page
    Component.onCompleted: audio.refresh()
    readonly property var mic: audio.inputs.find(i => i.name === audio.defaultInput) || null

    CcSection { title: "Speaker" }
    CcRow { title: "Volume"
        CcSlider { width: 460; from: 0; to: 100; unit: " %"; value: sys.volume; onMoved: (v) => sys.setVolume(v) } }
    Repeater {
        model: audio.outputs
        CcRow { title: modelData.label; description: modelData.name === audio.defaultOutput ? "In use" : ""
            CcButton { text: modelData.name === audio.defaultOutput ? "Selected" : "Use"; primary: modelData.name === audio.defaultOutput
                       onClicked: audio.setDefaultOutput(modelData.name) } }
    }

    CcSection { title: "Microphone" }
    Repeater {
        model: audio.inputs
        CcRow { title: modelData.label; description: modelData.name === audio.defaultInput ? "In use" : ""
            CcButton { text: modelData.name === audio.defaultInput ? "Selected" : "Use"; primary: modelData.name === audio.defaultInput
                       onClicked: audio.setDefaultInput(modelData.name) } }
    }
    CcRow { title: "Microphone level"; visible: !!page.mic
        CcSlider { width: 460; from: 0; to: 150; unit: " %"; value: page.mic ? page.mic.volume : 0
                   onMoved: (v) => audio.setInputVolume(audio.defaultInput, v) } }
    CcRow { title: "Mute microphone"; visible: !!page.mic
        CcSwitch { checked: page.mic ? page.mic.muted : false; onToggled: (v) => audio.setInputMute(audio.defaultInput, v) } }

    CcSection { title: "AI Noise-Canceling" }
    CcRow { title: "AI Noise-Canceling microphone"
            description: audio.noiseCancelAvailable ? "Removes background noise from your voice (RNNoise)."
                         : "Needs the RNNoise plugin: sudo pacman -S noise-suppression-for-voice"
        CcSwitch { enabledSwitch: false; checked: false } }
}
