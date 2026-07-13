# SHLabs Rikoshet

Four tempo-synced rhythmic effects for [VCV Rack 2](https://vcvrack.com) — a
rhythmic gate, a ping-pong delay, a pattern delay, and a crossfader. Feed them a
clock and they lock their motion to musical subdivisions, from two bars down to a
1/32 (with dotted and triplet values); pull the clock and they run from their own
tempo knob. They stay out of the way harmonically and put their character into
timing and stereo movement, with CV over the controls you'd actually want to
modulate.

**Free to download and use. GPL-3.0-or-later.**

## Modules

- **Gate** — tempo-synced rhythmic amplitude gate. Rate picks from 14 musical
  subdivisions; Shape morphs a hard square to a raised cosine; Pulse Width sets
  the duty cycle; a Stereo Offset slips the right channel for width. CV over Rate,
  Shape, Depth and PW.
- **PingPong** — stereo ping-pong delay with cross-fed feedback. Spread offsets
  the right-channel time for tumbling repeats, Cross sets how much feedback crosses
  sides, and low/high cut sit in the feedback path so repeats darken as they fade.
- **MultiTap** — an 8-step pattern delay across a tempo-synced window. Per-tap
  level sliders place the hits, alternating L/R panning with Spread opens the
  stereo image, Decay tapers the taps, and Feedback recirculates the whole pattern.
- **Blend** — a stereo A/B crossfader with input drive and an equal-power curve.
  Blend parallel chains of Rikoshet effects, or use it as a dry/wet around a serial
  one. CV over Mix and Drive.

## Install

Search **Rikoshet** in the [VCV Library](https://library.vcvrack.com) and click
Add, or download the latest `.vcvplugin` from
[Releases](https://github.com/shlabs-audio/rikoshet/releases), drop it into Rack's
user plugin folder, and restart Rack. The modules appear under the **SHLabs** brand.

Part of the [SHLabs](https://shlabs.ch) catalog.

## Build from source

Requires the [VCV Rack SDK](https://vcvrack.com/manual/Building#Setting-up-your-development-environment).

```sh
RACK_DIR=/path/to/Rack-SDK make install
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
