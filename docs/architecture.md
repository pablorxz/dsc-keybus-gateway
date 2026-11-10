# Prototype architecture

This document describes the implemented architecture. The later user-authorized
physical observations are recorded in `docs/physical-validation.md`; they do not
establish comprehensive physical compatibility or production readiness.

```text
physical Keybus (tested) ────────────┐
                                     ├─ dsc_core ── observed PanelState ─┐
synthetic frame/key model (bench) ────┘                                  │
                                                                        v
HA native API ─┐                                                dsc_app AlarmService
               ├─ validated operations ── bounded scheduler ── key FIFO ── dsc_core
HTTP dashboard ┘                             │
                                             └─ observed outcome / timeout
```

- `dsc_core` owns capture, the protocol transmit FIFO and decoding. Its snapshots
  distinguish known/unknown observations and invalidate them on disconnection.
  The ESP32 timer/GPIO details live inside this library. The simulator injects
  synthetic frames into the same decoder and consumes its actual key queue.
  `connected` indicates that clock activity was observed within the last three
  seconds; it does not prove that the data wire or every panel message is valid.
  The dashboard samples the last clock activity at each state poll for its
  connection indicator; the animation is not a separate panel heartbeat.
- `dsc_app` owns policy and operation sequencing. A bounded four-operation queue,
  with one reserved slot for an eligible exit-delay disarm, protects
  whole operations; the separate 32-key core FIFO manages wire transmission. A
  successful arm/disarm means the matching state was observed, not merely that
  keys were sent. Operations that lose contact after dispatch are uncertain; a
  timeout does not prove that the panel did nothing. Physical keypads cannot be
  locked by software and may interfere with a multi-key operation.
  When an arm request is waiting during an observed exit delay, an explicit
  disarm for that partition takes priority after any in-flight keys complete.
  Unstarted operations for that partition are rejected as superseded; the
  interrupted arm is uncertain until the panel reports its actual state. The
  disarm succeeds only when the panel reports the target state, never merely
  because its access code was transmitted. Other partitions retain their order.
  If an HTTP arm operation observes the panel requesting an access code, it can
  wait for a code attached to that operation ID, with the original timeout and
  a check that the panel still requests a code. Abandonment is uncertain and
  does not resend the arm key. Physical keypad input can still interfere.
- `components/dsc_alarm_panel` publishes native ESPHome/HA entities. The optional
  text sensor exposes the latest shared operation as `id:status:reason`; it is a
  snapshot, not a complete audit log or a promise to deliver every intermediate
  state during overlapping operations. Native actions do not return operation IDs.
- `components/dsc_web` serves the local dashboard and calls the same service.
  HTTP returns an operation ID which can be queried until it completes. Zone names,
  categories and display groups are configured at build time; they are not read
  from the DSC panel and groups are not partitions.
- The application keeps up to 64 observed changes in volatile memory for the
  dashboard; this is not the panel event buffer or a durable audit log.
- The sample model assumes a synthetic four-digit code, specific keypad prompts,
  an accelerated exit delay and synthetic frames. Its passing tests do not establish
  the exact timing or behavior of a real PC1864 installation.

The native API uses ESPHome Noise encryption. HTTP uses Digest authentication but
no TLS: command bodies, including alarm codes, are visible on the network, and
Digest is not complete replay protection. A deliberately stalled client delayed a
second HTTP request by about five seconds. Transport security and broader physical
validation are required before treating this prototype as production-ready.
