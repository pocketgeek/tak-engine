# Streaming to YouTube

At the title screen or in a game, press **F9**, or open **Settings → YouTube Streaming** at the title screen
or **Esc → YouTube Streaming** during a game.
Copy the stream key from YouTube Studio, click **Paste** beside the key field,
select the video settings, and click
**Start Streaming**. Follow the preview/status in YouTube Studio to make the
broadcast public. See [YouTube's encoder setup guide](https://support.google.com/youtube/answer/2907883).

The initial settings are Max Width 3840, 60 FPS and 6,000 Kbps video. The panel also offers
1080p, 1440p, 4K (3840×2160), Full Resolution, Max Width 3840, 60 FPS, 3,000–80,000 Kbps,
and automatic or CPU encoding. Full Resolution uses the actual window drawable
size at Start Streaming, including high-DPI scaling, rounded down to even pixels.
**Max Width 3840** scales down to at most 3840 pixels wide while preserving
the window’s aspect ratio, without adding a 16:9 border or upscaling smaller
windows. A 7680×2160 window produces 3840×1080. Dimensions are rounded down to
even pixels and calculated when the stream starts.

The output dimensions stay fixed until you stop and restart; resizing the window
letterboxes the new image. The supported input range is 2–8192 pixels per axis,
subject to the encoder’s limits. Selecting a larger resolution raises a lower
bitrate to 12,000 Kbps for 1440p or 30,000 Kbps for 4K; it remains adjustable. Automatic tries
available hardware H.264 encoders first. Above 4096 pixels on either axis, it
then tries hardware HEVC to preserve native dimensions. If none supports that
size, it reports failure instead of silently overloading the CPU. You can select
CPU explicitly, or choose a smaller resolution. At smaller sizes, automatic
CPU fallback remains available and is identified in the status line.

Hardware H.264 and HEVC backends:

| Platform | Hardware backends |
| --- | --- |
| Linux | NVIDIA NVENC; Intel/AMD VA-API |
| Windows x64 | NVIDIA NVENC; AMD AMF; Intel Quick Sync |
| macOS | Apple VideoToolbox |

Windows ARM64 builds include x264 CPU encoding. The pinned FFmpeg version does
not support NVENC on Windows ARM64, and Qualcomm hardware encoding is not
implemented. AMF and Quick Sync are compiled in but require a compatible native
driver; their availability on Windows ARM64 has not been validated.

A working GPU driver with the selected codec and resolution support is required for hardware
encoding. The panel reports the encoder actually opened, connection state,
encoded frames, missed output frames (**DROPPED**), superseded or busy capture
submissions (**REPLACED**), and estimated encoded-payload bitrate. Replacing a
capture does not itself mean an output frame was missed. Unsupported hardware falls back to the bundled x264 CPU encoder. Hardware
that fails during encoding triggers a restart using the CPU only at dimensions
up to 4096 pixels per axis; larger sizes report an error.

The stream contains the game view, HUD, cursor and mixed game audio, including
music. Hardware cursors temporarily use the software drawing path so they appear
in the stream. A differently shaped game window is letterboxed. Setup controls
and connection badges are drawn **after capture** and stay off-stream.
Microphone input, desktop capture, account login and broadcast scheduling are
not included.

**Stop Streaming** stops the upload; closing the F9 panel leaves it running.
Returning to the title screen keeps it running. Exiting the application stops it.
The key and preferences last for that application session
only. Keys are masked, never written to settings, and excluded from encoder and
network diagnostics. Ctrl+V pastes a key; Ctrl+A clears the key field.

Uploads use RTMPS with certificate and hostname verification, H.264 or HEVC video, AAC
stereo audio at 44.1 kHz / 128 Kbps, and two-second video keyframes. After a
connection failure, the worker retries three times, waiting 2, 4 and 8 seconds.
Stop interrupts network I/O; an unsuccessful stream reports failure in the game.
The primary YouTube ingest endpoint is currently fixed in the client.

## Performance and dependencies

Encoding, scaling, audio resampling and networking run on a separate worker.
The video queue holds only the latest frame; the audio ring has a fixed size.
Slow uploads cannot grow an unlimited queue or block the mixer/simulation.
The OpenGL renderer uses two pixel buffers and nonblocking fence checks for
readback. SDL's other renderers use its portable readback at the selected capture
rate; that readback can add render-thread cost. Lower the resolution/frame rate
or choose hardware encoding if streaming reduces game performance.

No FFmpeg executable or additional DLL/shared-library bundle is required by
players. FFmpeg, x264, the Linux OpenSSL/VA-API/DRM loaders and the Windows oneVPL
dispatcher are built as static archives. NVIDIA and AMD SDK headers add no runtime
library bundle. Installed GPU drivers and native OS frameworks remain external,
just as SDL's display/audio drivers do.

## Development checks

`tools/build-ffmpeg-bink.sh` now builds both Bink playback and streaming support.
Its pinned dependencies are built by `tools/build-stream-deps.sh`. Linux needs
Python 3, Ninja, Perl's standard modules (Fedora: `perl-core`), and patch in
addition to the regular build tools. Perl configures OpenSSL during the build;
it is not a game dependency. Windows uses Schannel and macOS uses
SecureTransport, so they do not build OpenSSL. FFmpeg's OpenSSL hostname checking
is patched explicitly for the pinned FFmpeg 7.1 source.

CMake requires the media/codec/TLS/loader archives by full path; CI additionally
checks executable imports. `stream_test` tests H.264/AAC output, timestamps,
configuration validation, stopping, failure and restart without any network.
`stream_capture_test` exercises capture with an open private setup panel.
Both are registered with CTest. For a GPU capture check:

```sh
SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=opengl \
  ./build-o2/stream_capture_test gl /tmp/capture.flv
./build-o2/stream_test h264_nvenc
```

The optional local network check uses a test-only build of the backend, a local
TLS proxy, and a local FFmpeg receiver. It verifies reconnect and rejects a
trusted certificate for the wrong hostname. It never contacts YouTube:

```sh
cmake --build build-o2 --target stream_network_test
python3 tools/check-stream-network.py build-o2/stream_network_test
```

The external `ffmpeg` and `openssl` commands used by this check are development
test tools, not engine runtime dependencies.

Encoder slowdowns skip late video frames without reconnecting. Audio catches up
independently; reconnects are reserved for connection/write failures. Title-screen
account and settings overlays are excluded from the broadcast. Title and lobby music is captured at the current master/music volume, then
audio switches to the game mixer when the match starts. Menu click sounds and
movie audio use separate players and are not captured. During loading
and modal screens, the last captured frame remains on the stream.

OpenGL capture downscales on the GPU before asynchronous readback when the
stream is smaller than the window. Frame buffers transfer to the encoder worker
without an extra full-frame copy on all render backends. The OpenGL path falls
back to full-size readback when framebuffer scaling is unavailable.

Native ultrawide streaming uses hardware HEVC when H.264 rejects its width.
YouTube lists HEVC as an accepted RTMPS codec in its
[encoder settings](https://support.google.com/youtube/answer/2853702?hl=en).
FFmpeg CPU SIMD paths are enabled for efficient RGB-to-YUV conversion. NASM is a
build-only dependency on x86; it adds no shipped runtime library.

NVIDIA capture feeds RGB directly to NVENC, which performs color conversion on
the GPU. Other hardware and CPU encoders use the SIMD-enabled FFmpeg converter.
