# Host-supplied Now Playing metadata

Any host embedding mIV Sound (based on EffeTune Mixwright) can supply the currently playing media's
title, album, artist, and artwork to Visualizer. This optional extension is
independent of the host name, audio processing, and the built-in media player.
Hosts that do not send metadata retain the existing empty metadata display.

## Message contract

After initializing the plug-in, query `Steinberg::Vst::IConnectionPoint` from
either its `IComponent` or `IEditController` and call `notify` on the UI thread.
For this extension, direct notification requires no extra `connect` handshake
or outgoing-message receiver. Do not send from the audio callback.

Set the `IMessage` ID to **`EffeTune.NowPlaying`**. This ID identifies version 1
of this contract; a breaking change will use a different ID. The attributes are:

| Attribute | VST3 type | Meaning and limit |
| --- | --- | --- |
| `title` | `setString` | Title, up to 4096 UTF-16 code units excluding the terminator |
| `album` | `setString` | Album, same limit |
| `artist` | `setString` | Artist, same limit |
| `artwork` | `setBinary` | Encoded image bytes, up to 8 MiB (8,388,608 bytes) |
| `artworkMimeType` | `setString` | `image/png`, `image/jpeg`, `image/gif`, or `image/webp` |

Strings must contain well-formed, null-terminated UTF-16. Artwork with nonempty
bytes requires its MIME attribute. Supply browser-decodable encoded image data
matching that MIME; the native receiver validates the transport and MIME, and
the WebView decodes the image. Successful notification does not certify that
the image can be decoded. Prefer PNG or JPEG for the widest support across
platforms and embedded WebView versions. Video thumbnails can be sent as artwork.

Every message replaces the complete snapshot. Missing or empty fields clear
the corresponding value; an empty or absent attribute list clears all metadata.
Send a fresh message on each media change and resend all values to retain them. Fields
outside this contract are ignored. Unknown message IDs retain the SDK's normal
handling and cannot change the snapshot.

`notify` returns `kResultOk` on acceptance. Null messages, missing message IDs,
invalid field types or UTF-16, excessive sizes, or nonempty artwork with an
unsupported MIME return `kInvalidArgument`; the previous snapshot is retained in
full. Check the result before considering the update delivered.

The plug-in copies accepted strings and artwork before `notify` returns. The
host can release the message immediately afterward. Metadata is private to the
plug-in instance, survives closing/reopening the editor and pipeline state
reloads, and is cleared by an explicit empty message or plug-in termination.
It is not saved in VST state, presets, or pipeline history. The host should send
the current snapshot after each new plug-in initialization.

## Host-side C++ example

This example uses the SDK's message-allocation helper and reference-counted
pointers. `pluginComponent` is the initialized component and `host` implements
`IHostApplication`. The artwork pointer remains valid through the call.

```cpp
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsthostapplication.h"

Steinberg::tresult sendNowPlaying(
    Steinberg::Vst::IComponent* pluginComponent,
    Steinberg::Vst::IHostApplication* host,
    const Steinberg::Vst::TChar* title,
    const Steinberg::Vst::TChar* album,
    const Steinberg::Vst::TChar* artist,
    const void* artwork, Steinberg::uint32 artworkBytes,
    const Steinberg::Vst::TChar* artworkMimeType)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    if (!pluginComponent || !host)
        return kInvalidArgument;
    FUnknownPtr<IConnectionPoint> receiver(pluginComponent);
    if (!receiver)
        return kNoInterface;
    auto message = owned(allocateMessage(host));
    if (!message)
        return kOutOfMemory;
    message->setMessageID("EffeTune.NowPlaying");
    auto* attributes = message->getAttributes();
    if (!attributes)
        return kInvalidArgument;
    if (title && attributes->setString("title", title) != kResultOk)
        return kInvalidArgument;
    if (album && attributes->setString("album", album) != kResultOk)
        return kInvalidArgument;
    if (artist && attributes->setString("artist", artist) != kResultOk)
        return kInvalidArgument;
    if (artworkBytes != 0) {
        if (!artwork || !artworkMimeType ||
            attributes->setString("artworkMimeType", artworkMimeType) != kResultOk ||
            attributes->setBinary("artwork", artwork, artworkBytes) != kResultOk)
            return kInvalidArgument;
    }
    return receiver->notify(message);
}
```

Pass null strings and zero artwork bytes to clear everything. To send text
without artwork, pass its strings and zero artwork bytes. No editor needs to
be open when the host sends either update.

## Native/WebView delivery

The plug-in caches the latest accepted snapshot and exposes it through the
existing `host/getInfo` startup and `telemetry/read` polling routes. Requests
carry the last `nowPlayingRevision` seen (zero for a fresh editor). Responses
always include `nowPlayingRevision`, and include `nowPlaying` only when the
revision differs. An empty snapshot is `null`; otherwise it contains
`title`, `album`, `artist`, and an `artwork` array containing `{src: dataURL}`
when artwork exists. Equal revisions omit the image payload. Reading a
snapshot does not consume it, so new editors can replay the cached value.

These JSON fields belong to the internal UI bridge. Hosts only implement the
VST3 message contract above.

VST3 API references: [IConnectionPoint](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IConnectionPoint.html),
[IAttributeList](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAttributeList.html).

## Host lyrics and playback position (Windows)

mIV Sound extends the same `EffeTune.NowPlaying` ID with optional attributes.
The EffeTune message IDs are retained for compatibility with existing hosts.

| Attribute | VST3 type | Meaning and limit |
| --- | --- | --- |
| `mediaGeneration` | `setInt` | Media generation, all 64 bits of the host's u64 carried by the VST integer |
| `lyricsSidecar` | `setString` | Decoded same-name `.lrc` text; up to 262,144 UTF-16 code units excluding the terminator |
| `lyricsEmbedded` | `setString` | Embedded lyrics text; same limit |
| `durationSeconds` | `setFloat` | Finite, nonnegative media duration in seconds; omit when unknown |

The string validation, full replacement, revision, ownership and transient-state
rules above also apply to these attributes. Missing lyrics clear their candidate;
missing generation or duration removes that field. Invalid types, malformed
UTF-16, excessive text, or invalid duration reject the entire replacement with
`kInvalidArgument`, leaving its revision and all previous values intact.
The receiver uses upstream `parseLrc` on both candidates and
`selectLyrics({ sidecar, embedded })` once per accepted Now Playing revision.
Selection therefore prefers timed sidecar, timed embedded, untimed sidecar,
then untimed embedded lyrics. Metadata-only text has no lyric lines.
As in upstream, selection bounds the displayed model to 2048 lines, a shared
32,768-character budget, 4096 characters per line, and 2048 timed words. The
262,144-unit transport limit is separate from these display limits.

Increment `mediaGeneration` on every media adoption (including A to B to A),
Local/Remote change, and clear. Completing artwork or lyrics changes the snapshot
revision but retains that media generation. Include the same generation in the
following independent position notification; delivery order need not match.
Older plug-ins ignore the new Now Playing attributes.

Send **`EffeTune.PlaybackPosition`** through the same UI-thread `notify` entry:

| Attribute | VST3 type | Meaning and validation |
| --- | --- | --- |
| `mediaGeneration` | `setInt` | Required; same u64 bit pattern as Now Playing |
| `available` | `setInt` | Required; exactly 0 or 1. With 0, all remaining attributes are ignored |
| `playing` | `setInt` | Required with available=1; exactly 0 (paused) or 1 (advancing) |
| `positionSeconds` | `setFloat` | Required with available=1; finite and nonnegative media seconds |
| `rate` | `setFloat` | Required with available=1; finite and positive, including while paused |
| `hostTimeNanoseconds` | `setInt` | Required with available=1; nonnegative QPC-derived nanoseconds when the position was read |

The Windows receiver assumes the host and MSVC `std::chrono::steady_clock` use the
same QPC clock and nanosecond epoch, without a process-relative origin. At response
creation the native receiver returns `position + (now - hostTime) * rate` while
playing, and the unchanged position while paused (bounded below by zero).
The host has already accounted for plug-in latency; the receiver does not subtract
it again. Use available=0 for Remote, stopped playback, or absent media. Missing or
invalid required attributes reject the notification with `kInvalidArgument` and
preserve the previous complete position. Only the latest position is retained;
it is instance-local, survives editor closure and VST state reloads, is never
serialized, and is cleared on termination. Resend both snapshots after plug-in
initialization. No delivery retries or ordering protocol are added by this extension.

Both `host/getInfo` and `telemetry/read` include an extrapolated `playback` object
with `mediaGeneration`, `available`, `playing`, `positionSeconds`, and `rate`.
The two flags are JSON integers. Generation uses a decimal **string** in both
internal UI snapshots, preserving all u64 bits across JavaScript number limits;
it is absent before any generation has been supplied. This internal encoding does
not change the host-facing VST type. Lyrics travel only when the Now Playing
revision changes. JS interpolates from response receipt with `performance.now()`.
Lyrics require equal media generations, available=1, and a fresh position for the
current display generation. On becoming visible, the UI marks its position stale
before drawing; a request issued before that visibility change cannot clear it.
If a hidden poll was processed, the first resumed poll discards queued telemetry
and the next poll fetches a fresh position. A brief hide without a processed
hidden poll reads a fresh position directly; visibility changes do not change
`telemetryWasHidden`. Paused lyrics remain visible at a fixed time.

The Visualizer Lyrics item and the built-in Lyric Stage presets use this host
provider, including the upstream Offset and fade settings. The pinned v0.15.0
Lyric Stage presets (16:9 and 9:16) contain no Counter items and are retained
unchanged. Counter and Counter Gallery remain excluded, and imported Counters
still receive no position. Host playback is independent of `hostTransport` and
does not change ProcessContext handling. The built-in player and its `.lrc` file
attachment workflow remain excluded; lyrics come from the host. Remote delivery
is controlled by the host and needs no special receiver behavior.
