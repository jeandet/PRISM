"""16_lab_instrument.py — benchtop DAQ console: power supply, generator, scope.

Demonstrates:
  - prism.plot_field() + vb.canvas() as the instrument's screen, redrawn from the controls
  - prism.slider(label=, unit=, step=): a control that prints its own value
  - prism.derived(): readouts computed from several controls, following what they read
  - prism.shared(): the three setpoints the hardware thread reads, and the frame it sends back
  - prism.channel(): ordered hardware events, each one a line in the log
  - a background prism.worker() that never touches a Field

Run:
  PYTHONPATH=builddir/python python3 python/examples/16_lab_instrument.py
  PYTHONPATH=builddir/python python3 python/examples/16_lab_instrument.py --headless
"""

import math
import sys
import time

import prism

WINDOW_SECONDS = 0.002  # the scope's sweep: 2 ms across the screen
SAMPLE_COUNT = 401


def waveform(name, phase):
    """One normalized sample of `name` at `phase` (in cycles), in [-1, 1]."""
    phase -= math.floor(phase)
    if name == "Square":
        return 1.0 if phase < 0.5 else -1.0
    if name == "Triangle":
        return 4.0 * phase - 1.0 if phase < 0.5 else 3.0 - 4.0 * phase
    if name == "Sawtooth":
        return 2.0 * phase - 1.0
    return math.sin(2.0 * math.pi * phase)


def stamp(seconds):
    return f"[{seconds:7.3f}s]"


class LabInstrument(prism.Model):
    # DC power supply
    voltage = prism.slider(12.0, min=0.0, max=30.0, step=0.1, label="Voltage", unit="V")
    current_limit = prism.slider(2.0, min=0.1, max=5.0, step=0.05, label="Current limit", unit="A")
    output_on = prism.button("Output", checked=True)
    output_state = prism.derived(lambda self: "Output: ON" if self.output_on.checked else "Output: OFF")
    max_power = prism.derived(lambda self: f"Max power {self.voltage.value * self.current_limit.value:.1f} W")
    trip_btn = prism.button("Trip over-current protection")
    reset_btn = prism.button("Reset protection circuit")

    # Function generator
    wave = prism.choice(["Sine", "Square", "Triangle", "Sawtooth"], default="Sine")
    frequency = prism.slider(1000.0, min=50.0, max=20000.0, step=50.0, label="Frequency", unit="Hz")
    amplitude = prism.slider(5.0, min=0.5, max=10.0, step=0.1, label="Amplitude", unit="Vpp")
    offset = prism.slider(0.0, min=-4.0, max=4.0, step=0.1, label="Offset", unit="V")
    period_caption = prism.derived(lambda self: f"Period {1e3 / self.frequency.value:.3f} ms")

    # The screen, and the transport to the hardware thread: one Shared slot per value the worker
    # reads, so it never touches a Field (a Field belongs to the logic thread).
    scope = prism.plot_field()
    v_set = prism.shared(12.0)
    i_limit = prism.shared(2.0)
    out_on = prism.shared(True)
    fault = prism.shared(False)
    reading = prism.shared("V: 0.00 V | I: 0.00 A | P: 0.0 W | 25.0 C")
    events = prism.channel("")
    log = prism.list_field([])

    def view(self, vb):
        vb.group("Oscilloscope", lambda: vb.canvas(self.scope))
        vb.group(
            "DC Power Supply",
            lambda: vb.vstack(
                self.output_on,
                self.output_state,
                self.voltage,
                self.current_limit,
                self.max_power,
                self.trip_btn,
                self.reset_btn,
            ),
        )
        vb.group(
            "Function Generator",
            lambda: vb.vstack(
                self.wave,
                self.frequency,
                self.amplitude,
                self.offset,
                self.period_caption,
            ),
        )
        vb.group("Telemetry", lambda: vb.vstack(self.reading))
        vb.group("Event Log", lambda: vb.list(self.log))

    @prism.on_change(voltage, current_limit, output_on, immediate=True)
    def publish_setpoints(self):
        self.v_set.value = self.voltage.value
        self.i_limit.value = self.current_limit.value
        self.out_on.value = bool(self.output_on.checked)

    @prism.on_change(frequency, amplitude, offset, wave, immediate=True)
    def redraw_scope(self):
        frequency = self.frequency.value
        trigger = (
            round(time.monotonic() * frequency) / frequency
        )  # sweep starts on a rising edge
        start = trigger - WINDOW_SECONDS / 2
        step = WINDOW_SECONDS / (SAMPLE_COUNT - 1)
        xs = [i * step * 1e6 for i in range(SAMPLE_COUNT)]
        ys = [
            self.offset.value
            + self.amplitude.value
            / 2
            * waveform(self.wave.value, (start + i * step) * frequency)
            for i in range(SAMPLE_COUNT)
        ]
        self.scope.replace_series([(xs, ys, "#3cffb4")], thickness=1.8)
        self.scope.set_labels(x="time (us)", y="volts")

    @prism.on_change(trip_btn)
    def trip_protection(self):
        self.fault.value = True

    @prism.on_change(reset_btn)
    def reset_protection(self):
        self.fault.value = False


def hardware_frame(v_set, i_limit, out_on, fault, now):
    """The instrument's answer to a read: (volts, amps, watts, celsius, constant-current?)."""
    if fault:
        return 0.0, i_limit * 1.25, 0.0, 65.0, True
    if not out_on:
        return 0.02 * math.sin(now * 2.0), 0.0, 0.0, 25.0, False
    load = 8.0 + 0.5 * math.sin(now * 0.3)
    if v_set / load > i_limit:  # current compliance
        volts = i_limit * load
        return volts, i_limit, volts * i_limit, 28.0 + volts * i_limit * 0.85, True
    current = v_set / load
    return v_set, current, v_set * current, 28.0 + v_set * current * 0.85, False


def main():
    app = LabInstrument()
    start = time.monotonic()

    def log_line(message):
        app.log.push(f"{stamp(time.monotonic() - start)} {message}")
        if app.log.size() > 150:
            app.log.erase(0)

    app.events.observe(log_line)
    log_line("[INFO] PRISM DAQ engine ready")
    log_line("[INFO] Virtual instrument on /dev/usbtmc0")

    cc_state = {"cc": False}

    def hardware_tick():
        now = time.monotonic() - start
        volts, current, power, temperature, cc = hardware_frame(
            app.v_set.value, app.i_limit.value, app.out_on.value, app.fault.value, now
        )
        app.reading.value = (
            f"V: {volts:5.2f} V | I: {current:5.2f} A | P: {power:6.1f} W"
            f" | {temperature:4.1f} C | {'CC' if cc else 'CV'}"
        )
        if cc != cc_state["cc"]:
            cc_state["cc"] = cc
            mode = "constant-current" if cc else "constant-voltage"
            app.events.send(f"{stamp(now)} [INFO] supply entered {mode} mode")

    prism.worker(hardware_tick, interval=0.025)  # ~40 Hz, like the C++ bench

    if "--headless" in sys.argv:
        prism.run(app, headless_seconds=0.5)
        print("lab instrument headless verification OK")
    else:
        prism.run(app, title="PRISM Lab Instrument - Python")


if __name__ == "__main__":
    main()
