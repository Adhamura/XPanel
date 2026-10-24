import QtQuick

// Pop-ups of the Manual TDP, CPU Clock and GPU Clock tiles (shown in the Quick Panel's Chooser).
QtObject {
    id: tp
    property var chooser

    function refresh(items) {
        const keep = chooser.index
        chooser.items = items
        chooser.index = Math.min(keep, items.length - 1)
    }

    // ---- Manual TDP ----
    function tdpItems(w, lock) {
        return [
            { stepper: true, label: qsTr("SPL"), value: w, min: device.value("tdpMin") || 7, max: device.value("tdpMax") || 30,
              step: 1, unit: " W" },
            { label: qsTr("Lock boost to SPL"), value: "lock", selected: lock,
              sub: lock ? qsTr("On") : qsTr("Off (+5 / +10 W)") }
        ]
    }
    function openTdp() {
        if (!device.connected) return
        let lock = !!device.value("tdpLock")
        chooser.show(qsTr("Manual TDP"), tdpItems(device.value("tdp") || 15, lock), () => {
            lock = !lock
            device.setValue("tdpLock", lock)
            refresh(tdpItems(chooser.items[0].value, lock))
            return true
        }, (it, v) => {
            device.setValue("tdp", v)
            if (device.value("opmode") !== "manual") device.setValue("opmode", "manual")
        })
    }

    // ---- CPU / GPU clock ----
    // The daemon keeps 0 for "no limit"; here that is the hardware bound.
    // Modes: auto (the driver decides), range (you set min and max), fixed (you set one clock).
    readonly property var clockKeys: ({
        cpu: { title: qsTr("CPU Clock"), minKey: "cpuClockMin", maxKey: "cpuClock", lo: "cpuMin", hi: "cpuMax", step: 100 },
        gpu: { title: qsTr("GPU Clock"), minKey: "gpuClockMin", maxKey: "gpuClock", lo: "gpuMin", hi: "gpuMax", step: 100 }
    })
    readonly property var clockModes: ["auto", "range", "fixed"]
    function modeLabel(m) {
        return m === "fixed" ? qsTr("Fixed (you decide)") : m === "range" ? qsTr("Range (you decide)") : qsTr("Auto (driver decides)")
    }
    function clockLimits(kind) {
        const k = clockKeys[kind]
        const lo = device.value(k.lo) || 0, hi = device.value(k.hi) || 0
        return { lo: lo, hi: hi, min: device.value(k.minKey) || lo, max: device.value(k.maxKey) || hi }
    }
    function clockMode(kind) {
        const k = clockKeys[kind], c = clockLimits(kind)
        if (!device.value(k.minKey) && !device.value(k.maxKey))
            return config.get("clock." + kind + ".mode", "auto") === "range" ? "range" : "auto"
        return c.min === c.max ? "fixed" : "range"
    }
    function setClockLimits(kind, min, max) {
        const k = clockKeys[kind], c = clockLimits(kind)
        device.setValue(k.minKey, min <= c.lo ? 0 : min)
        device.setValue(k.maxKey, max >= c.hi ? 0 : max)
    }
    function clockItems(kind, mode, min, max) {
        const c = clockLimits(kind), step = clockKeys[kind].step
        const items = [{ label: qsTr("Mode"), value: "mode", sub: modeLabel(mode), selected: mode !== "auto" }]
        const stepper = (which, label, value) => ({ stepper: true, which: which, label: label, value: value,
                                                    min: c.lo, max: c.hi, step: step, unit: " MHz" })
        if (mode === "range")
            items.push(stepper("max", qsTr("Maximum"), max), stepper("min", qsTr("Minimum"), min))
        else if (mode === "fixed")
            items.push(stepper("fixed", qsTr("Clock"), min))
        return items
    }
    function applyMode(kind, mode) {
        const c = clockLimits(kind), key = "clock." + kind + "."
        config.set(key + "mode", mode)
        if (mode === "auto") {
            setClockLimits(kind, c.lo, c.hi)
            return { min: c.lo, max: c.hi }
        }
        if (mode === "fixed") {
            const v = config.get(key + "fixed", Math.round((c.lo + c.hi) / 2 / 100) * 100)
            setClockLimits(kind, v, v)
            return { min: v, max: v }
        }
        const r = config.get(key + "range", [c.lo, c.hi])
        setClockLimits(kind, r[0], r[1])
        return { min: r[0], max: r[1] }
    }
    function openClock(kind) {
        if (!device.connected) return
        const c = clockLimits(kind)
        if (!c.hi) {
            chooser.show(clockKeys[kind].title, [{ label: qsTr("Not available"), value: "none",
                                                   sub: qsTr("Update xpanel-daemon") }], null)
            return
        }
        let mode = clockMode(kind)
        chooser.show(clockKeys[kind].title, clockItems(kind, mode, c.min, c.max), () => {
            mode = clockModes[(clockModes.indexOf(mode) + 1) % clockModes.length]
            const v = applyMode(kind, mode)
            refresh(clockItems(kind, mode, v.min, v.max))
            return true
        }, (it, v) => {
            const key = "clock." + kind + "."
            if (it.which === "fixed") {
                config.set(key + "fixed", v)
                setClockLimits(kind, v, v)
                refresh(clockItems(kind, mode, v, v))
                return
            }
            let max = chooser.items[1].value, min = chooser.items[2].value
            if (it.which === "min" && min > max) max = min
            if (it.which === "max" && max < min) min = max
            config.set(key + "range", [min, max])
            setClockLimits(kind, min, max)
            refresh(clockItems(kind, mode, min, max))
        })
    }
    function tileInfo(kind) {
        const c = clockLimits(kind), mode = clockMode(kind)
        if (mode === "auto") return { value: qsTr("Auto") }
        return { value: mode === "fixed" ? c.min + " MHz" : c.min + "–" + c.max, badge: qsTr("ON") }
    }
}
