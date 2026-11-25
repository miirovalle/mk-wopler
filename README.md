# MK WOPLER

MK WOPLER is an AUv3 reimplementation of the **MWobbler** distorting wobble
filter, written for **iPadOS 17** (and buildable for macOS / Windows for
auditioning).

MWobbler is a MeldaProduction product. This project is a clean-room
reimplementation of the behaviour described in its user manual: no Melda code,
binaries or resources are used or included. The name is used only to describe
what the filter is modelled on.

---

## Why this shape of project

Nothing loadable was supplied — the only input was `MWobbler_intro.pdf`, a copy of
the plugin manual. So "reverse engineering" here means **reading the documented
behaviour and rebuilding it from the specification**. iPadOS 17 only loads one
kind of plugin: an **AUv3 app extension**. There is no VST2/VST3/AUv2 on iOS.

The code is therefore split so that everything except the plugin shell is
independent of the plugin framework:

```
Source/dsp/        framework-free C++17 DSP core (testable anywhere)
Tests/test_dsp.cpp offline harness: 51 checks, no GUI, no host
Source/            JUCE AUv3/VST3 wrapper + parameter mapping
Source/ui/         custom look and feel, response graph, oscillator editor, meters
```

---

## Signal chain

Reproduced from the manual's description ("2 processors with distortion and 98
filters", "LFO and follower with side-chain"):

```
input ─┬─ [optional 6-band Linkwitz-Riley split] ─┐
       │                                          │
       │   per band:                                │
       │     ┌───────────────────────────────┐      │
       │     │ Filter processor 1            │      │
       │     │  drive → filter → saturation  │      │
       │     │  → output gain → clip         │      │
       │     │  (own LFO + level follower)   │      │
       │     └───────────────┬───────────────┘      │
       │     ┌───────────────▼───────────────┐      │
       │     │ Filter processor 2 (identical)│      │
       │     └───────────────┬───────────────┘      │
       └─────────────────────┼──────────────────────┘
                             ▼
              global saturation → output gain → clipper → dry/wet
```

Each of the two processors owns an independent oscillator and follower, exactly
as the manual describes, and either can drive frequency, resonance, gain and
character modulation.

## The 98 filter types

`wob::kFilterTypeNames` enumerates them; `wob::filterTypeInfo()` maps each to a
family and slope. All of them are implemented, not stubbed:

| Family | Count | Notes |
| --- | --- | --- |
| Low-pass / high-pass | 13 + 13 | 6 … 120 dB/oct; 6 dB is a true one-pole, the rest are Butterworth cascades |
| Band-pass / notch / peak / low shelf / high shelf | 4 / 2 / 2 / 2 / 2 | 12 … 48 dB/oct |
| Fade | 3 | LP↔HP, LP↔BP↔HP, LP↔notch cross-fades driven by Character |
| Harmonics | 4 | power-of-two or linear partial banks, normal and swapped |
| Sub-X / Sub-X soft / Over-X / Band-X | 4 | LP/HP/BP body plus a character-distributed partial bank |
| Formant | 6 | five vowels plus an A-E-I-O-U morph across Character |
| Comb | 8 | 1–3 tap kernels, damped feedback |
| Diffuser | 8 | 2–8 all-pass stages, increasing kernel density |
| Polymorph | 20 | four-section structures morphed by Character |
| Scream | 7 | 1–7 resonant sections with a soft-clipped feedback path |

All filtering uses a topology-preserving state-variable structure, so the
coefficients can be swept every control period without the instability a naive
biquad update produces — that is what makes fast wobbles work.

## Oscillator (LFO)

* 16 predefined shapes (exponential, triangle, sine powers, harmonics,
  disharmonics, rectangle, rect-saw, saw, noise, mess) interpolated continuously.
* Drawable 64-point custom curve, blended by the Custom amount.
* 32-step sequencer, blended by the Step amount.
* 256-partial additive mode (levels and phases), plus a Convert button that
  analyses the current shape into partials.
* Rate 0.01–100 Hz, or host-synced note lengths (straight / triplet / dotted).
* Phase offset, stereo width (per-channel phase offset), invert, smoothing and
  direct override.

## Follower

Attack, release, RMS length, max level, an optional side-chain equaliser
(LP/HP/BP/shelves/bell) and an optional external side-chain bus. Depth crossfades
between the oscillator (0 %) and the follower (100 %), as documented.

---

## Building

### Desktop (Windows / macOS) — for auditioning

```bash
cmake -S . -B build
cmake --build build --config Release --target MKWOPLER_VST3 MKWOPLER_Standalone
```

JUCE is picked up from `External/JUCE` if present, otherwise fetched at configure
time (pinned to 8.0.9).

### iPadOS 17 AUv3

Building the AUv3 requires macOS with Xcode 15 or newer and an Apple Developer
account for signing. Windows cannot produce an iOS app extension.

On iOS an AUv3 extension cannot be installed on its own: it has to ship inside a
container app. `CMakeLists.txt` therefore requests `AUv3 Standalone` for iOS, and
JUCE embeds the `.appex` into the Standalone app via `XCODE_EMBED_APP_EXTENSIONS`.
Building `MKWOPLER_Standalone` is what produces the complete, packageable bundle.

```bash
cmake -S . -B build-ios -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphoneos \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0

cmake --build build-ios --config Release --target MKWOPLER_Standalone
```

Then in Xcode:

1. Open `build-ios/MKWOPLER.xcodeproj`.
2. Select the `MKWOPLER_Standalone` (app) and `MKWOPLER_AUv3` (extension) targets
   and set your team under *Signing & Capabilities*.
3. Run the app target on the device. The plugin then appears in hosts such as
   **AUM, Cubasis, GarageBand, Drambo, Loopy Pro and apeMatrix** as
   `Wobble Audio: MK WOPLER`.

### Installable .ipa from GitHub Actions

[`.github/workflows/ios-ipa.yml`](.github/workflows/ios-ipa.yml) produces an
`.ipa` on a `macos-14` runner.

**Unsigned build** — runs on every push and packages `Payload/MK WOPLER.app`
(container app + embedded `PlugIns/*.appex`) into `MK-WOPLER-unsigned.ipa`. It
verifies that the extension really is inside the bundle and prints the
`NSExtensionPointIdentifier` so a broken embed fails the build instead of
shipping.

> An unsigned `.ipa` will **not** install on a stock iPad. iOS verifies
> signatures at install time. Re-sign it with Sideloadly / AltStore / Xcode, or
> use the signed job.

**Signed build** — manual only (`Run workflow` → *Build a signed IPA*). It needs
these repository secrets:

| Secret | Contents |
| --- | --- |
| `IOS_CERT_P12_BASE64` | Developer or Ad-hoc `.p12`, base64 encoded |
| `IOS_CERT_P12_PASSWORD` | password for that `.p12` |
| `IOS_TEAM_ID` | Apple Developer team id |
| `IOS_SIGNING_IDENTITY` | optional, e.g. `Apple Development` (the default) or `Apple Distribution` |
| `IOS_PROFILE_APP_BASE64` | `.mobileprovision` for the container app |
| `IOS_PROFILE_APP_NAME` | that profile's name, as shown in Xcode |
| `IOS_PROFILE_AUV3_BASE64` | `.mobileprovision` for the AUv3 extension |
| `IOS_PROFILE_AUV3_NAME` | that profile's name |

Two App IDs have to exist in the developer portal, because an AUv3 bundle is two
bundles:

| Bundle | Bundle identifier |
| --- | --- |
| Container app | `com.wobbleaudio.mkwopler` |
| AUv3 extension | `com.wobbleaudio.mkwopler.mkwoplerAUv3` |

The extension identifier is not hand-picked: JUCE derives it as
`<bundle_id>.<last component>AUv3` in `extras/Build/CMake/JUCEUtils.cmake`, so
changing `BUNDLE_ID` in `CMakeLists.txt` moves both IDs. Register the two App
IDs, generate a profile for each, and paste their names into the two
`*_PROFILE_*_NAME` secrets.

Once installed, the app itself only hosts the extension — open AUM (or any other
AUv3 host) and the plugin is listed under `Wobble Audio` as `MK WOPLER`.

Note that free developer accounts expire after 7 days and require re-signing;
paid accounts give one year.

## iPadOS notes

* The editor is laid out for a 940 × 680 window and scales with the host's pane;
  it does not assume a mouse — every control is a tap-and-drag.
* `fullState` carries the 202 parameters *plus* the non-automatable oscillator
  data (custom curves, step sequences, 256-partial spectra) so AUM/Cubasis
  sessions restore exactly.
* Oversampling (1× / 2× / 4×) is exposed as a parameter but is intentionally not
  an automation target: changing it reallocates the oversampler and re-prepares
  the engine, which is not a real-time-safe operation.

## Running the tests

```bash
cmake --build build --target wobble_tests
ctest --test-dir build --output-on-failure
```

`wobble_tests --render` additionally writes `wobble_reference.wav` — a three
second 55 Hz tone swept by a 4 Hz saw through a resonant low-pass, useful for
sanity-checking by ear.

The suite covers: the parameter table layout and ranges, stability of all 98
filter types on an impulse, low-pass/high-pass pass-band and stop-band behaviour,
resonance gain, harmonic generation, oscillator shape/phase/width/invert/sync,
override, the engine's dry/wet transparency, output-gain accuracy, six-band mode,
band disable, side-chain, and boundedness under heavy drive.

---

## Deliberate differences from the original

These are choices, not oversights:

* **Multiband** is a serial Linkwitz-Riley split with per-band overrides
  (level, frequency, resonance, gain, character, drive, oscillator phase/rate
  multiplier). The base product's two-processor mode is the default; the 6-band
  mode is the MB variant's behaviour approximated with a shared parameter set
  rather than six fully independent copies of every control.
* **Easy screen** exposes a curated set of macro controls rather than Melda's
  saved multiparameter devices, which are a host-agnostic preset system of their
  own.
* **Randomisation, preset browser, online preset exchange, analyser/sonogram and
  the full 98-type polymorph editor** described in the manual are not
  implemented; the response graph, oscillator display and meters are.
* **Resonance curve** is a squared exponential (flat at 0 %, self-oscillating
  region at 100 %) tuned so the 40 % default stays musical.
