# ImTube

**ImTube** is a lightweight YouTube-style video application designed for embedded Linux systems, with a primary focus on the **STM32MP257F**.

The project is built around **Dear ImGui**, **SDL3**, **GLES/Vulkan**, and **yt-dlp**, with hardware-accelerated video playback provided by the underlying Linux multimedia stack.

The rendering API is abstracted behind a small `RenderBackend` interface with two implementations: the default **OpenGL ES 3.2** backend (runs on both a regular Linux PC via Mesa and on the STM32MP257F via the VeriSilicon `gcnano` driver) and an optional **Vulkan** backend for the PC. GLES is the default because the STM32MP25 Vulkan driver is still immature.

The goal is to create a fast, responsive and resource-efficient video interface without the overhead of a large desktop GUI framework such as Qt or GTK.

---

## Features

Planned features include:

* YouTube video browsing
* YouTube search
* Video playback
* Hardware-accelerated video decoding
* 1080p video playback
* Video thumbnails
* Play/pause controls
* Volume control
* Seek/progress control
* Full-screen playback
* Keyboard and mouse support
* Touchscreen support
* Lightweight embedded UI
* OpenGL ES 3.2 rendering (Vulkan optional, PC)

---

## Target Hardware

The primary target platform is:

**STM32MP257F**

The STM32MP257F provides:

* Dual Arm Cortex-A35 CPU
* Hardware video acceleration
* 3D GPU (VeriSilicon, OpenGL ES 3.1/3.2 + Vulkan 1.2/1.3)
* Linux/OpenSTLinux support

The initial target is an embedded Linux system based on **OpenSTLinux**.

The project is particularly intended for systems with limited CPU and RAM resources where a traditional desktop GUI framework would introduce unnecessary overhead.

---

## Architecture

ImTube is designed around a small number of major components:

```text
                    ┌──────────────────────┐
                    │        ImTube        │
                    └──────────┬───────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              ▼                ▼                ▼
        Dear ImGui           yt-dlp          GStreamer
              │                │                │
              │                │                ▼
              │                │          HW Video Decoder
              │                │                │
              ▼                │                ▼
            Vulkan             │              DMABUF
              │                │                │
              └────────────────┼────────────────┘
                               │
                               ▼
                         STM32MP257F GPU
                               │
                               ▼
                            Display
```

The GUI and rendering path is intentionally separated from the video pipeline.

### GUI

**Dear ImGui** is used for the application interface.

ImGui is well suited for this project because it provides a lightweight immediate-mode GUI without requiring a large widget framework.

### Graphics

**OpenGL ES 3.2** is the default rendering API, exposed through a small
`RenderBackend` interface (`src/render/RenderBackend.h`) with two
implementations:

* `GlesContext` - OpenGL ES 3.2 through SDL/EGL. Works on any Linux PC (Mesa)
  and on the STM32MP257F (VeriSilicon `gcnano`). **This is the default.**
* `VulkanContext` - Vulkan through SDL. PC-focused; kept as an optional backend.

The goal is to make the STM32MP257F GPU responsible for rendering the user
interface rather than relying heavily on the CPU.

### Platform / Input

**SDL3** is the preferred platform layer.

SDL3 provides Linux support and access to graphics, input, audio and Vulkan-related functionality.

SDL3 will be used for:

* Window/display management
* Keyboard
* Mouse
* Touch input
* Game controllers
* Vulkan integration
* Platform abstraction

SDL2 may be used as a fallback if required by the target OpenSTLinux environment.

### Video

**GStreamer** will be used for video playback.

The long-term goal is to keep the video path as close as possible to zero-copy:

```text
Network
   │
   ▼
GStreamer
   │
   ▼
H.264 / video decoder
   │
   ▼
DMABUF
   │
   ▼
Vulkan / GPU
   │
   ▼
Display
```

This minimizes unnecessary CPU-side frame copying.

---

## YouTube Integration

ImTube will use **yt-dlp** for extracting video information and media URLs.

yt-dlp is a feature-rich command-line audio/video downloader supporting thousands of sites and is an active project derived from youtube-dl.

The intended architecture is:

```text
                YouTube
                   │
                   ▼
                 yt-dlp
                   │
                   ▼
          Video / Audio URL
                   │
                   ▼
               GStreamer
                   │
                   ▼
              HW Decoder
                   │
                   ▼
                 Display
```

ImTube will not necessarily download an entire video before playback.

The preferred approach is to obtain the appropriate media stream information and allow GStreamer to handle streaming and buffering.

---

## Why Dear ImGui?

The project deliberately avoids Qt6 and GTK4 for the primary UI.

Qt6 and GTK4 are powerful and mature GUI frameworks, but they also provide a large amount of functionality that is unnecessary for a dedicated embedded video application.

ImTube instead aims for:

```text
Application
     │
     ▼
Dear ImGui
     │
     ▼
Vulkan
     │
     ▼
GPU
```

This keeps the GUI stack small and gives the application direct control over rendering.

---

## Why GLES (and when Vulkan)?

OpenGL ES 3.2 was chosen as the default backend because it is the one graphics
API guaranteed to work on **both** a regular Linux PC (Mesa) and the
STM32MP257F (VeriSilicon `gcnano` driver). The STM32MP25 Vulkan driver
(`libvulkan_VSI.so`) is still immature per the ST community forum, so Vulkan is
kept as an optional PC-only backend.

```text
CPU
 │
 ├── Application logic
 ├── Network control
 └── UI generation
          │
          ▼
   GLES 3.2 / Vulkan
          │
          ▼
         GPU
          │
          ▼
       Display
```

The CPU should primarily handle application logic while the GPU handles rendering.

The backend is selected at configure time:

```sh
cmake -S . -B build -DIMTUBE_RENDERER=gles     # default, portable
cmake -S . -B build-vk -DIMTUBE_RENDERER=vulkan # optional, PC only
```

---

## Why SDL3?

SDL3 is the preferred platform abstraction layer because it provides a relatively small and portable interface for:

* Linux
* ARM
* Input
* Windows
* Display handling
* Vulkan
* Audio
* Controllers

SDL3 officially supports Linux and provides Vulkan-related graphics functionality.

The project will target SDL3 first.

If SDL3 proves problematic in the STM32MP257F/OpenSTLinux environment, the implementation may fall back to SDL2.

---

## Resource Efficiency

Resource efficiency is one of the primary design goals of ImTube.

The project is intended for embedded hardware rather than desktop PCs.

The desired characteristics are:

| Component         | Goal                 |
| ----------------- | -------------------- |
| GUI               | Dear ImGui           |
| Rendering         | GLES 3.2 (default) / Vulkan |
| Platform          | SDL3                 |
| Video             | GStreamer            |
| Decoder           | Hardware accelerated |
| Memory            | Minimize copies      |
| CPU usage         | Keep low             |
| GPU usage         | Utilize hardware     |
| Target resolution | 1920×1080            |
| Target hardware   | STM32MP257F          |

The project prioritizes efficient GPU rendering and hardware video decoding.

---

## STM32MP257F

The primary development platform is the **STM32MP257F**.

The project is being developed with embedded Linux/OpenSTLinux in mind.

The target environment is approximately:

```text
STM32MP257F
     │
     ├── Cortex-A35
     │
     ├── 3D GPU (VeriSilicon, GLES 3.2)
     │
     ├── Hardware Video Decoder (H.264, V4L2)
     │
     └── Linux / OpenSTLinux
             │
             ├── Wayland / Weston
             ├── GLES (gcnano)
             ├── GStreamer
             └── SDL3
```

---

## Development Status

**Early development**

The GUI, rendering and search/playback plumbing are functional on a Linux PC;
the STM32MP257F port is in progress.

Development status:

* [x] SDL3 + GLES (and Vulkan) initialization
* [x] Dear ImGui integration
* [x] Basic ImTube interface
* [x] yt-dlp integration
* [x] YouTube search
* [x] Thumbnail support
* [x] GStreamer integration
* [ ] STM32MP257F rendering (gcnano GLES)
* [ ] H.264 hardware decoding
* [ ] 720p playback on hardware
* [ ] 1080p playback on hardware
* [ ] Touch interface
* [ ] Full-screen playback
* [ ] Zero-copy / DMABUF optimization

---

## Building

### Native (Linux PC)

Dependencies: CMake >= 3.24, a C++20 compiler, `pkg-config`, GStreamer 1.0 dev
packages (`gstreamer-1.0`, `gstreamer-app-1.0`, `gstreamer-video-1.0`), OpenGL ES
headers (`glesv2`, `egl`), and `libcurl`. SDL3 and Dear ImGui are fetched and
built by CMake automatically.

```sh
cmake -S . -B build -G Ninja                 # default: GLES 3.2 backend
cmake --build build
./build/imtube
```

Vulkan variant (optional):

```sh
cmake -S . -B build-vk -G Ninja -DIMTUBE_RENDERER=vulkan
cmake --build build-vk
./build-vk/imtube
```

### STM32MP257F (OpenSTLinux SDK, cross-compiled)

Install the OpenSTLinux SDK for the STM32MP2 series, then:

```sh
source /path/to/sdk/environment-setup-cortexa35-ostl-linux
cmake -S . -B build-stm32mp2 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-stm32mp2.cmake
cmake --build build-stm32mp2
```

The toolchain file forces `IMTUBE_RENDERER=gles`, builds SDL3 with the Wayland
video driver (for Weston) and enables the `IMTUBE_EMBEDDED` tweaks (yt-dlp picks
a single H.264 stream so no ffmpeg merge is needed; playback feeds the VPU
decoder). Run the result on the target with:

```sh
./imtube --playlist            # whatever the embedded launch args end up being
```

At runtime the app needs `yt-dlp` on the target, GStreamer with the H.264
hardware elements, and a running Wayland compositor (Weston).

---

## Example UI

The intended interface is similar to a lightweight YouTube client:

```text
┌─────────────────────────────────────────────────────────┐
│ ImTube                                      🔍   ⚙      │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────────────┐  ┌──────────────────┐             │
│  │                  │  │                  │             │
│  │    Thumbnail     │  │    Thumbnail     │             │
│  │                  │  │                  │             │
│  └──────────────────┘  └──────────────────┘             │
│                                                         │
│  ┌──────────────────┐  ┌──────────────────┐             │
│  │                  │  │                  │             │
│  │    Thumbnail     │  │    Thumbnail     │             │
│  │                  │  │                  │             │
│  └──────────────────┘  └──────────────────┘             │
│                                                         │
├─────────────────────────────────────────────────────────┤
│ ▶  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  🔊 1080p            │
└─────────────────────────────────────────────────────────┘
```

---

## Dependencies

The initial target stack is:

* C++
* Dear ImGui
* SDL3
* OpenGL ES 3.2 (default) / Vulkan (optional)
* GStreamer
* yt-dlp
* OpenSTLinux
* Wayland / Weston

SDL3 can be built for Linux using its CMake build system.

---

## License

The licensing model for ImTube has not yet been finalized.

Individual third-party components retain their respective licenses.

In particular, ImTube uses projects such as:

* Dear ImGui
* SDL3
* GStreamer
* yt-dlp
* Vulkan

Refer to each project's official documentation and license for details.

---

## Project Goal

The goal of ImTube is simple:

> **Create a lightweight, GPU-accelerated YouTube-style video player for embedded ARM Linux systems.**

Rather than bringing a complete desktop environment to an embedded device, ImTube aims to provide only what is needed:

```text
          Lightweight GUI
                +
             Vulkan
                +
          Hardware Video
                +
             GStreamer
                +
             yt-dlp
                =
              ImTube
```

The primary goal is to make **1080p video playback and a responsive graphical interface possible on the STM32MP257F while keeping CPU, RAM and software overhead as low as reasonably possible.**
