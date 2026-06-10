# SHLabs Rikoshet

Multitap rhythm and gate-combination modules for [VCV Rack 2](https://vcvrack.com).
One input clock in, rich rhythmic polyphony out — tap a clock through
configurable delay arrays and gate-logic combinators and get back polyrhythms
that would otherwise take a rack-row of cabling to assemble by hand.

**Free to download and use.**

## Modules

- **Gate** — tempo-synced rhythmic amplitude gate (tremolo / pattern gate).
- **PingPong** — stereo ping-pong multitap with per-tap timing and feedback.
- **MultiTap** — the core multitap engine: one trigger in, an array of derived gates out.
- **Blend** — recombiner that mixes and gate-logics taps into new rhythms.

## Install

Download the latest `.vcvplugin` from [Releases](https://github.com/shlabs-audio/rikoshet/releases),
drop it into Rack's user plugin folder, and restart Rack. The modules appear
under the **SHLabs** brand.

Part of the [SHLabs](https://shlabs.ch) catalog.

## Build from source

```sh
RACK_DIR=/path/to/Rack-SDK make install
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
