# Dillo for STM32MP

Dillo for STM32MP is a lightweight port and build environment for the **Dillo Web Browser** targeting STMicroelectronics STM32MP-based embedded Linux systems.

The project is primarily intended for the **STM32MP257F** and similar STM32MP platforms running a Linux system generated with **Yocto/OpenEmbedded**.

The goal is to provide a small, fast and resource-efficient graphical web browser suitable for embedded systems where the resource requirements of Chromium or Firefox may be undesirable.

## Target Platform

The primary target platform is:

```text
STM32MP257F
├── ARM Cortex-A35
├── Linux
├── Wayland
├── Weston
└── Dillo
      └── HTML / CSS
```

The project is particularly suited to custom STM32MP257F boards running a Yocto-based Linux distribution.

## Goals

The main goals of this project are:

- 🚀 Fast startup
- 🪶 Low resource consumption
- 💾 Small storage footprint
- 🧠 Low memory usage
- ⚡ Low CPU overhead
- 🖥️ Embedded graphical browser
- 🐧 Linux support
- 🏗️ Cross-compilation for ARM64
- 🌐 Wayland/Weston integration
- 🔧 Easy integration with Yocto-based systems
- 📦 Reproducible builds

The project is intended primarily for **embedded applications and controlled web interfaces**, rather than general-purpose modern web browsing.

## Why Dillo on STM32MP?

Modern browsers such as Chromium and Firefox provide excellent compatibility with today's web, but they also come with a substantial software footprint.

A modern browser may include:

- Large browser engines
- JavaScript runtimes
- Complex graphics subsystems
- Extensive browser APIs
- Sandboxing infrastructure
- Multiple processes
- Multimedia frameworks
- Large memory requirements

For an embedded device that only needs to display a relatively simple HTML/CSS interface, much of this functionality may not be necessary.

Dillo takes a much smaller approach.

```text
Modern browser:

Application
     │
     ▼
Browser engine
     │
     ├── JavaScript
     ├── Web APIs
     ├── Multimedia
     ├── GPU frameworks
     ├── Sandbox
     └── Many other components
     │
     ▼
Operating system


Dillo:

Application
     │
     ▼
Dillo
     │
     ▼
FLTK
     │
     ▼
Wayland
     │
     ▼
Weston
     │
     ▼
Linux
     │
     ▼
STM32MP257F
```

This makes Dillo an interesting candidate for lightweight embedded graphical interfaces.

## STM32MP257F

The **STM32MP257F** provides a powerful ARM64 processing platform for embedded Linux applications while still being significantly more constrained than a typical desktop computer.

Dillo can be used to provide a graphical HTML interface on such a platform without requiring a complete desktop browser stack.

A possible system architecture is:

```text
                    STM32MP257F
                         │
                         ▼
                       Linux
                         │
                         ▼
                      Wayland
                         │
                         ▼
                       Weston
                         │
                         ▼
                       FLTK
                         │
                         ▼
                       Dillo
                         │
                         ▼
                     HTML / CSS
                         │
                         ▼
                      Display
```

For example, the display output can ultimately be connected through the STM32MP257F display pipeline to HDMI, DSI or another supported display interface.

## Wayland and Weston

The target environment for this project is **Wayland**, using **Weston** as the compositor.

The intended architecture is:

```text
Dillo
  │
  ▼
FLTK
  │
  ▼
Wayland
  │
  ▼
Weston
  │
  ▼
DRM / KMS
  │
  ▼
STM32MP257F Display Pipeline
```

This avoids requiring a traditional X11 desktop environment.

The project therefore targets embedded systems where Weston is already used as part of the graphical stack.

## Cross Compilation

Dillo is compiled on an x86-64 Linux development machine and executed on the ARM64 STM32MP257F target.

The recommended approach is to use the **Yocto SDK** generated for the target system.

```text
Development PC
x86-64 Linux
      │
      │ Yocto SDK
      ▼
AArch64 Cross Compiler
      │
      ▼
Dillo
      │
      ▼
ARM64 Linux Binary
      │
      ▼
STM32MP257F
```

Yocto provides the cross compiler, linker, headers and target sysroot required to build software for the STM32MP257F.

For example:

```bash
bitbake st-image-weston -c populate_sdk
```

The generated SDK can then be installed on the development machine and used by CMake.

## CMake Build

The project uses CMake to provide a convenient cross-compilation environment.

A typical build may look like:

```text
Dillo source
     │
     ▼
CMake
     │
     ▼
Yocto SDK
     │
     ▼
AArch64 binary
     │
     ▼
STM32MP257F
```

This allows Dillo to be developed and tested independently from the complete Yocto image.

The Yocto SDK supplies the libraries and headers corresponding to the target Linux system.

## Development Workflow

A typical development workflow is:

```text
1. Build Yocto image
          │
          ▼
2. Generate Yocto SDK
          │
          ▼
3. Install SDK on development PC
          │
          ▼
4. Configure CMake
          │
          ▼
5. Cross-compile Dillo
          │
          ▼
6. Copy binary to STM32MP257F
          │
          ▼
7. Run Dillo under Weston
          │
          ▼
8. Test and optimize
```

This approach makes it possible to iterate on Dillo without rebuilding the complete Yocto image for every source-code change.

## Embedded Applications

Dillo is particularly interesting when the displayed web content is under the control of the device developer.

Potential applications include:

- Industrial HMIs
- Control panels
- Device configuration
- Hardware monitoring
- Diagnostics
- Information displays
- Embedded dashboards
- Kiosks
- Network configuration
- Local documentation
- Device management interfaces

For example:

```text
STM32MP257F
     │
     ├── Embedded application
     │
     ├── Local web server
     │       │
     │       └── HTML / CSS
     │
     └── Dillo
             │
             ▼
          Display
```

The embedded application can generate a lightweight HTML interface that Dillo displays locally.

## Advantages

### Low Resource Usage

Dillo is designed around a significantly smaller feature set than modern browser engines.

This can reduce:

- RAM usage
- CPU usage
- Storage requirements
- Startup time
- Software complexity

This is particularly useful on embedded Linux systems.

### Fast Startup

The relatively small browser architecture allows Dillo to start quickly compared with large modern browser engines.

This is useful when the browser forms part of the device's main graphical interface.

### Small Footprint

Dillo can be integrated into systems where storage and memory are important constraints.

This makes it an interesting alternative to much larger browser solutions.

### Simple Architecture

Dillo does not attempt to implement the complete modern web platform.

For controlled HTML/CSS interfaces, this reduced complexity can be an advantage.

## Limitations

Dillo is **not a replacement for Chromium or Firefox** when modern web compatibility is required.

### JavaScript

Modern JavaScript-heavy applications may not work correctly.

Examples include many:

- Single-page applications
- Modern dashboards
- Web applications
- Social media sites
- Online office applications

### Modern CSS

Dillo supports a subset of modern web technologies. Complex CSS layouts may therefore render differently from modern browsers.

### Multimedia

Dillo is not designed to be a modern multimedia browser.

Websites depending on:

- HTML5 video
- Modern audio APIs
- DRM
- Advanced streaming platforms

may not work.

### Modern Web APIs

Dillo does not provide the same web platform as Chromium or Firefox.

Technologies such as:

- WebGL
- WebRTC
- Service Workers
- Modern JavaScript APIs
- Advanced browser APIs

are outside the primary scope of the project.

## Dillo vs. Modern Browsers

| Feature | Dillo | Chromium / Firefox |
|---|---:|---:|
| Startup time | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| RAM requirements | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| CPU overhead | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| Storage footprint | ⭐⭐⭐⭐⭐ | ⭐ |
| Basic HTML | ✅ | ✅ |
| Basic CSS | ✅ | ✅ |
| Modern JavaScript | ❌ / Limited | ✅ |
| Modern CSS | ❌ / Limited | ✅ |
| WebGL | ❌ / Limited | ✅ |
| Modern Web APIs | ❌ / Limited | ✅ |
| Modern video streaming | ❌ / Limited | ✅ |
| Embedded Linux | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| Modern websites | ⭐ | ⭐⭐⭐⭐⭐ |

The ratings are intended as general qualitative comparisons rather than hardware benchmarks.

## Yocto Integration

The target operating system for development is based on **Yocto/OpenEmbedded**.

The recommended approach is to use Yocto to provide the target SDK and sysroot while using CMake to build Dillo independently.

A typical environment is:

```text
Yocto / OpenEmbedded
        │
        ├── Linux
        ├── Wayland
        ├── Weston
        ├── Target libraries
        └── Cross compiler
                │
                ▼
             CMake
                │
                ▼
             Dillo
                │
                ▼
          STM32MP257F
```

This approach keeps the Dillo source and build system separate from the BSP and allows the browser to be developed independently.

Once the browser has been successfully tested on the target platform, it can optionally be integrated into the final Yocto image.

## Project Status

This project is focused on making Dillo practical to build and run on STM32MP-based embedded Linux systems.

The primary development target is:

```text
Hardware:
    STM32MP257F

Architecture:
    ARM64 / AArch64

Operating System:
    Linux

Build System:
    Yocto / OpenEmbedded

Graphics:
    Wayland

Compositor:
    Weston

Browser:
    Dillo

Build System:
    CMake
```

## Philosophy

The goal of this project is not to turn Dillo into another Chromium.

Instead, the objective is to provide a **small, fast and practical HTML/CSS browser for embedded Linux systems**.

The project focuses on:

- Small footprint
- Fast startup
- Low memory consumption
- Low CPU usage
- ARM64 support
- Wayland support
- Embedded Linux
- Simple deployment
- Reproducible cross-compilation

## Summary

Dillo is an interesting browser for embedded systems where **resource efficiency is more important than complete modern web compatibility**.

The STM32MP257F provides a capable ARM64 embedded Linux platform, while Yocto provides the cross-compilation environment and target sysroot required to build software for the device.

The goal of this project is to combine these technologies into a simple development workflow:

```text
                Yocto
                  │
                  ▼
              SDK / Sysroot
                  │
                  ▼
                CMake
                  │
                  ▼
                Dillo
                  │
                  ▼
               FLTK
                  │
                  ▼
              Wayland
                  │
                  ▼
               Weston
                  │
                  ▼
            STM32MP257F
                  │
                  ▼
                HDMI
                  │
                  ▼
              Display
```

The result is intended to be a lightweight graphical browser suitable for embedded HTML/CSS interfaces on STM32MP platforms.

---

## License

Please refer to the Dillo project's official repository and documentation for the current licensing information.

## Links

- Dillo Project: [Dillo Project](https://dillo-browser.org/)
- Dillo Source Code: [Dillo Source Code](https://git.dillo-browser.org/dillo)
- FLTK: [FLTK Project](https://www.fltk.org/)
- Yocto Project: [Yocto Project](https://www.yoctoproject.org/)
