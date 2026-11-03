# mIV Sound

mIV Sound is a modified version of [EffeTune Mixwright](https://github.com/Frieve-A/effetune-mixwright)
by Frieve, based on upstream **v0.15.0** (`8c9a0a5`), for audio adjustment in
[mImageViewer](https://mikage.to/mimageviewer/). Display version: **0.15.0-miv.3**;
numeric build version: **0.15.0.1**. Please direct inquiries about this version
to mImageViewer.

This version has its own product name, VST3 class IDs and musical-note icons.
Settings, presets, SFZ references and WebView2 data live under the absolute
`MIV_SOUND_DATA_DIR` supplied by mImageViewer (`plugin/` and `webview/`). A missing,
empty or relative value fails initialization. On Windows, a non-empty
`WEBVIEW2_USER_DATA_FOLDER` also fails initialization because it overrides
WebView2's explicit storage path. Preset extensions and the state
format remain compatible. Counter is available in the add menu and Counter Gallery
in the system presets. Counters display mImageViewer's media playback position
using the same clock as host lyrics, including on tracks without lyrics. They hold
while paused and follow seeks, speed changes, track changes and loops. Without an
available host position (including stopped playback and Remote), they display zero.
Clicking a Counter does not reset it or seek. Playlist index remains zero because
the host does not supply it. ProcessContext sample counts are not used for Counters.
Copyright notices and upstream MIT licenses are preserved.

On Windows, initialize the submodules, install VS 2026 BuildTools, CMake 4.3 and
Node 22, then run `build-miv.bat` (`build-miv.bat --no-test` skips CTest).
The script uses the bundled Ninja and `FETCHCONTENT_UPDATES_DISCONNECTED=ON`:
repeat configuration uses the cached VST3 SDK while still checking pin changes.
An unfetched pin fails configuration instead of using the old SDK; acquire it
with `FETCHCONTENT_UPDATES_DISCONNECTED=OFF` before rebuilding.
The script returns 2/3/4 for configure/build/test failures (1 for setup).
Output: `build/windows-release/VST3/Release/mIV Sound.vst3`.
CTest supplies separate disposable `MIV_SOUND_DATA_DIR` paths for tests.

## mIV Original Visualizer

`ui-shim/presets/miv-original.json` defines **mIV Original** for all five aspect
ratios. The asset build inserts it first in each system preset file, with piano
roll notes, artwork and track details, a spectrum, and a two-line lyrics caption.
The spectrum uses a cyan-to-violet frequency gradient. White caption text and
its shadow sit over an 80% black band, separating lyrics from the bars behind it.
Timed lyrics show the current and next lines. Untimed lyrics scroll through the
caption with broad top and bottom fades, including the dark backplate. The
caption grows upward from its fixed bottom edge and reserves an unfaded middle
band for both complete timed rows, with line spacing of 1.25.
Each fade is one row high, keeping the visible half of an edge-centered row at
no more than half opacity. The portrait 9:16 caption uses 10% smaller text to
limit its coverage of the spectrum; other aspects keep their text sizes.
The caption and its backplate disappear when lyrics are unavailable.

After initialization checks succeed and before creating a WebView, mIV Sound
checks for both `plugin/config.json` and
the `webview/` profile under `MIV_SOUND_DATA_DIR`. Only when both are absent does
it save **Last view (Default)** as the startup preference. Last view reopens the last
Effects or Visualizer view across restarts; without a valid saved view it opens
Visualizer. Each view change saves the view once through the shared configuration
writer. Saving failures are logged; the next launch uses the last successfully
saved view, or Visualizer if none exists. Existing startup preferences and saved
layouts are never changed by this default. Existing users can select
**Settings → Configuration → Startup → Last view** (「前回の表示」) to enable it.
Without a saved layout, Visualizer loads the first
16:9 system preset, mIV Original. The tour does not start automatically in this
view; it remains available from **Settings → Take the tour**.
Rejected initialization writes no first-run config. Editor creation and attachment
are refused until initialization succeeds.
When no pipeline state is saved, the UI creates Volume at **0 dB** followed by
Level Meter. Volume remains available for later adjustment. Existing pipelines
and presets keep their saved gain, including values from earlier versions.
The VST UI reports browser input as not configured, using upstream's explicit
no-input preference; audio comes from the host. Input recovery is unnecessary,
so no missing-input or resume action appears. The shared capture boundary still
refuses browser input as a backup. Page-visibility tracking and display-DSP
gating retain their upstream behavior. On Windows, WebView2 microphone and camera permissions
are denied without a prompt and logged to debugger output. Other permissions keep their existing
behavior, including CHOC's clipboard-read allowance.

The upstream README is retained below for reference.

---

# EffeTune Mixwright

EffeTune Mixwright brings EffeTune's C++ DSP engine and desktop interface to the VST® 3 format. Windows x64 is the primary target, with macOS arm64 and x86_64 as secondary targets. The plug-in handles one to eight audio channels and supports five-bus processing pipelines, a WebView interface, A/B state management, telemetry, and 1×, 2×, 4×, and 8× oversampling.

## Requirements

- Git (Windows users must run `git config --global core.longpaths true`)
- CMake 3.24 or later, Ninja, Node.js 22 or later, and Python 3.10 or later
- Windows: Visual Studio 2022 Build Tools with the **Desktop development with C++** workload, plus the WebView2 Runtime
- macOS: A current version of Xcode and its Command Line Tools

Release builds for Windows enable AVX2 and FMA in the resampler by default. On systems without AVX2 support, configure the project with `-DEFFETUNE_ENABLE_AVX2=OFF` to select the SSE2 implementation instead.

## Clone and Build

```sh
git clone --recursive <repository-url> effetune-vst
cd effetune-vst
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

On Windows, run these commands from the x64 Native Tools Command Prompt for VS 2022. On macOS, replace the preset name with `mac-release`.

The main build artifacts are:

- VST3: `build/windows-release/VST3/Release/EffeTune Mixwright.vst3`
- Headless host: `build/windows-release/src/tools/effetune-headless.exe`
- WebView assets: `build/windows-release/webview-assets`

## Windows Editor Startup

WebView resources retain extended path prefixes and use native path separators,
including when the host loads the module through a `\\?\` path.
WebView2 creation failures show `EFFETUNE-UI-RUNTIME` with installation or repair
guidance. `EFFETUNE-UI-TIMEOUT` indicates an operation that did not finish within
the editor's startup deadline; it does not imply that a browser process started.
Detailed creation errors go to the Windows debugger output.

Windows configuration generates a narrowly adapted CHOC WebView header with
creation-error callbacks using [tools/build-choc-webview-header.mjs](tools/build-choc-webview-header.mjs).
The pinned CHOC source, license, and version metadata remain intact. The generator
rejects unexpected changes to the construction code when the dependency is updated.

## Visualizer and Saved Data

Use the **Visualizer** header button to view and edit analyzer layouts fed by the
native pipeline output. The **Effect Pipeline** button returns to effect editing.
Visualizer presets and imported images use the VST WebView's local storage.
The music player and music library remain outside the VST interface.
Upstream's LAN remote control is not available in this VST build.
The first-run tour explains DAW audio input and the supported views; reopen it
with **Settings → Take the tour**. **Settings → Help** opens the guide.
Visualizer supports Custom Analog Meter detectors and dials, plus per-item
**Edge Fade** masks. Counter items and the Counter Gallery presets display the
host's project time, including stopped positions, seeks, and loops. They use the
host sample rate regardless of oversampling; clicking a Counter does not reset
it. Negative project positions display as zero. A host that supplies no process
context displays zero until it provides a position.
The desktop Browser and Video playback remain outside the VST interface.
The add-item menu omits Lyrics and Video, and the bundled presets omit
Lyric Stage. Lyrics items in existing or imported layouts and user presets remain
editable; live player lyrics are unavailable because audio comes from the DAW.
Saved or imported Video items retain their layout data, with playback unavailable.

Use **Settings → Configuration → Controllers** to open controller mapping settings.
Controller mappings can toggle master bypass, switch A/B, and step through saved
presets. Player transport controls are omitted from the mapping choices.

**Configuration → General** includes the upstream language and theme choices.
It also selects sine or bandpass noise for frequency audition and the SFZ bank
size limit (64, 128, 256, 512, or 1024 MiB; 256 MiB by default).
**Startup** selects Last view, Effect Pipeline or Visualizer when the editor opens;
the pipeline itself continues to come from the host's plug-in state.
**Display** selects instantaneous or Peak Hold spectra for per-effect Spectrum
Overlays, automatic or manual pipeline columns, and Visualizer HDR rendering.
Automatic columns adapt to the editor width while retaining the saved manual
count. These preferences are saved in the Mixwright UI configuration or, for HDR,
the Visualizer's local storage.

**Settings → Backup / Restore** transfers saved pipeline, effect and Visualizer
presets, impulse responses and measurements in an `.effetune_backup` archive.
Choose a destination in the native save dialog; restoration previews the selected
items before writing them. Archive creation is limited to 256 MB. Native export
stages the archive and replaces the chosen file only after all chunks arrive.

The pinned upstream uses Normal PCM transport for per-effect Spectrum Overlays.
When built against upstream's native Spectrum Tap API, Mixwright uses the shared
Normal/HQ analyzer in After/Compare modes and exposes the HQ quality setting.
Availability depends on the analyzer accepting the engine's oversampled rate;
unsupported capture rates disable spectrum analysis without interrupting audio.
The bridge also publishes capture generations, positions, analysis window age and
remaining tap delay. Visual synchronization is not enabled yet: mapping these
positions to the host output clock, applying additional audio output delay and
reporting that applied delay to the host are still required. Visualizer analyzer
sources have their own analysis settings.

SFZ Note Player reads instruments and samples directly from their original files.
Open **SFZ Library…**, choose **Import files…** for selected instruments or
**Import folder…** to register a folder, then **Load** an instrument. Search,
filename/recent-import sorting, metadata badges, and bulk deletion are available.
ARIA `.bank.xml` definitions alongside the instruments are used during preparation.
File and folder references persist locally; the source files must
remain available, and moving or deleting them makes the instrument unavailable
until you import it again. **Delete** removes the local library reference and
leaves the source files in place. Projects store local SFZ identifiers, so choose
the instrument again when opening a project on another machine. The configured limit applies to each bank's preparation and
decoded sample data, with a native SFZ asset budget of 1 GiB. Impulse-response
assets retain their separate 32 MiB per-asset and 128 MiB total budgets.

When an active Notes analyzer and SFZ Note Player read the same input with matching
note ranges, Mixwright reuses their note analysis where routing and timing allow it.
Changing the input, note range or an intervening audio effect restores independent
SFZ analysis.

## Room EQ Measurements

Electron and VST WebViews intentionally keep separate browser-storage profiles and origins.
To reuse a measurement, export it as JSON from EffeTune, open Room EQ in EffeTune Mixwright,
and choose **Import...** beside the measurement list. The imported copy receives a new local
identifier and remains available to other Room EQ instances in the same VST WebView profile.
Select an imported measurement and choose **Delete** to remove that local copy and its stored
impulse responses after confirmation. Room EQ instances that reference the selected copy switch
to **No measurement** and aligned bypass before the local data is removed.

Enable **Include impulse responses in measurement JSON exports** before exporting when Room EQ's
phase-correction mode needs impulse-response data. Minimum- and linear-phase magnitude correction
can use exports without impulse responses. Import is limited to explicitly selected JSON files of
at most 128 MB.

## Headless Processing

```sh
build/windows-release/src/tools/effetune-headless.exe \
  --input input.wav --output output.wav --gain-db -6
```

The headless host processes WAV files through a fixed Volume pipeline backed by EffeTune's native DSP engine. Audio is processed in blocks of at most 128 frames. Run the executable with `--help` to list all available options.

## Verification

The standard `ctest` suite exercises the upstream DSP, the VST wrapper, state and bridge handling, resampler behavior, UI assets, WebView loading, and the Steinberg Validator. To compare every golden test vector against both the JavaScript and native DSP implementations, run:

```sh
node external/effetune/tools/dsp-parity/run.mjs --native \
  --native-runner ../../build/windows-release/external/effetune/dsp/effetune-dsp-parity-runner.exe
```

The relative path passed to `--native-runner` is resolved from `external/effetune`.

To check unpublished upstream changes without editing the submodule, configure a
separate local build with an explicit source directory:

```sh
cmake --preset windows-release -B build/spectrum-preview \
  -DEFFETUNE_UPSTREAM_SOURCE_DIR=D:/program/proto/effetune
cmake --build build/spectrum-preview --parallel 2
ctest --test-dir build/spectrum-preview --output-on-failure --parallel 2 --timeout 1800
```

The override supplies both shared DSP and WebView sources. Shipping presets still
default to the pinned submodule. Native Spectrum Tap builds can additionally run
`node D:/program/proto/effetune/tools/verify-spectrum-tap.mjs build/spectrum-preview/external/effetune/dsp/effetune_dsp_spectrum_tap_tests.exe`
to verify Normal/HQ parity with the browser.

Latency-changing parameter and asset updates are serviced while audio callbacks continue,
whether transport is playing or stopped. The audio owner captures the current pipeline;
the control service prepares compensation and bypass storage; a later audio-block boundary
applies the matching wet plan, bypass delay, and host latency together. Stale preparations
are recaptured, and allocation failures retain the applied plan with a deferred diagnostic
and bounded retry. Retired storage is reclaimed only by the control service. Host latency
notifications remain debounced and describe applied, not merely prepared, compensation.
Retiming preserves available recent delay history, but does not guarantee click-free
changes or recover samples older than the previous delay capacity.

To benchmark the resampler in a Release build, run `build/windows-release/src/tools/effetune-resampler-bench.exe`.

pluginval at strictness level 10, compatibility testing in target DAWs, testing on physical macOS hardware, code signing, and notarization are separate release-QA steps.

## License

Every distribution must include [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

The notices include JSZip's bundled lie, immediate, setImmediate, and pako
components, including the zlib-derived source notices. The bundle and the
interface's third-party notices viewer contain the same complete text.

VST is a registered trademark of Steinberg Media Technologies GmbH.
