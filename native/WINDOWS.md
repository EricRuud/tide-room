# Tide Room — Windows x64 preview

This is an experimental standalone build for Intel/AMD x64 PCs on Windows 10 or
11. It uses
the same instruments, spatial audio, Worn tape, 821 models, presets and recorder
as the Mac app. No external audio plugin is required.

## Run the preview

Extract the entire ZIP to a folder before opening `Tide Room.exe`. Keep the
`Resources` folder beside the executable: it contains the 821 models and license
notices. Choose a Windows audio output in **Options**, use headphones, and start
with 48 kHz and a 1024-sample buffer. The 821 mode requires 48 kHz.

This build uses Windows audio (WASAPI), with no bundled ASIO driver. User presets
are stored in `%APPDATA%/Tide Room/Presets`; recordings go into the user's Music
folder under `Tide Room Recordings`. Existing `.tideroom` presets can be imported.

## Preview limitations

- The room uses the existing CPU renderer with reduced rendering resolution.
  Glass towers, water, note lighting and local refraction remain; the Mac Metal
  camera prism pass is unavailable. Fullscreen rendering can be slower.
- Automated builds check DSP, presets, recording and opening the app. Listening,
  audio-device latency and interactive frame rate still need a real Windows PC.
- The executable is not code-signed; Windows may display a publisher/reputation
  warning for a downloaded preview.

## Build from source

Install Visual Studio 2022 with **Desktop development with C++** and a Windows
SDK, plus CMake 3.22 or newer and Git. In PowerShell at the repository root:

```powershell
./native/build-room.ps1 -RunChecks
```

The script fetches and verifies JUCE 8.0.9, builds x64 Release with the static MSVC
runtime, verifies the bundled model hashes and writes
`build-windows/Tide-Room-Windows-x64-preview.zip`. Omit `-RunChecks` for an app-only
build. The GitHub **Windows x64 preview** workflow runs the same script and uploads
the ZIP and check results. It does not publish a release.

Tide Room's original code is AGPL-3.0-only. See `Resources/LICENSE.txt` and
`Resources/THIRD_PARTY_NOTICES.md` for licensing and credits.
