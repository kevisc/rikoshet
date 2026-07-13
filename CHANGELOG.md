# Rikoshet changelog

## 2.0.1 — 2026-07-08

Launch-polish release. No new modules; fixes and refinements across the family
ahead of the first public announcement.

- **MultiTap** — fixed the feedback path, which previously fed back the summed
  taps and so ran away well below the labelled maximum. Feedback now recirculates
  the whole pattern with a loop gain equal to the Feedback knob, so it stays
  controlled up to ~90 % and darkens naturally each pass through the high-cut.
- **Blend** — removed a discontinuity in the Drive stage that could click when
  Drive was swept or CV-modulated near zero. The drive curve is now continuous and
  exactly transparent at zero.
- **All modules** — added dry-signal-through **bypass** (right-click ▸ Bypass now
  passes audio instead of muting), and clamped the on-panel tempo readout to the
  engine's BPM range so the display can't disagree with what you hear on a fast
  clock.
- Corrected the plugin metadata (author contact, homepage, source and donate URLs)
  to the SHLabs brand, and tidied the panels (bottom rule alignment, removed a
  footer label that overlapped the corner screw).

## 2.0.0 — 2026-05-24

Initial build for VCV Rack 2. Four tempo-synced rhythmic effects under the SHLabs
brand:

- **Gate** — rhythmic amplitude gate: 14 musical subdivisions, square-to-cosine
  shape morph, pulse width, stereo offset; CV over Rate, Shape, Depth, PW.
- **PingPong** — stereo cross-fed ping-pong delay: sync or free time, Spread,
  Cross, and low/high cut in the feedback path; CV over Time, Feedback, Mix.
- **MultiTap** — 8-step pattern delay over a tempo-synced window: per-tap level
  sliders, alternating pan with Spread, level Decay, and pattern feedback; CV over
  Window, Decay, Feedback, Mix.
- **Blend** — stereo A/B crossfader with input drive and an equal-power curve; CV
  over Mix and Drive.

> Rack ABI note: the major version is `2` to match VCV Rack 2. There is no `1.x`
> line — this is the first Rikoshet release series.
