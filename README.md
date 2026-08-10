# ImTube

**ImTube** is a lightweight YouTube-style video application designed for embedded Linux systems, with a primary focus on the **STM32MP257F**.

The project is built around **Dear ImGui**, **Vulkan**, **SDL3**, and **yt-dlp**, with hardware-accelerated video playback provided by the underlying Linux multimedia stack.

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
* Vulkan-based rendering

---

## Target Hardware

The primary target platform is:

**STM32MP257F**

The STM32MP257F provides:

* Dual Arm Cortex-A35 CPU
* Hardware video acceleration
* 3D GPU
* Vulkan-capable graphics stack
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

**Vulkan** is used as the primary rendering API.

The goal is to make the STM32MP257F GPU responsible for rendering the user interface rather than relying heavily on the CPU.

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

## Why Vulkan?

Vulkan allows ImTube to make direct use of the STM32MP257F's GPU.

The project aims to avoid unnecessary CPU rendering and framebuffer copies.

The desired rendering path is:

```text
CPU
 │
 ├── Application logic
 ├── Network control
 └── UI generation
          │
          ▼
       Vulkan
          │
          ▼
         GPU
          │
          ▼
       Display
```

The CPU should primarily handle application logic while the GPU handles rendering.

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
| Rendering         | Vulkan               |
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
     ├── 3D GPU
     │
     ├── Hardware Video Decoder
     │
     └── Linux / OpenSTLinux
             │
             ├── Wayland / Weston
             ├── Vulkan
             ├── GStreamer
             └── SDL3
```

---

## Development Status

**Early development**

The architecture is currently being developed and tested.

Planned development stages:

* [ ] SDL3 + Vulkan initialization
* [ ] Dear ImGui integration
* [ ] STM32MP257F Vulkan rendering
* [ ] Basic ImTube interface
* [ ] yt-dlp integration
* [ ] GStreamer integration
* [ ] H.264 hardware decoding
* [ ] 720p playback
* [ ] 1080p playback
* [ ] Thumbnail support
* [ ] YouTube search
* [ ] Touch interface
* [ ] Full-screen playback
* [ ] Zero-copy / DMABUF optimization

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
* Vulkan
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
