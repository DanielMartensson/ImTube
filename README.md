# Dillo Web Browser

Dillo is a lightweight graphical web browser designed with simplicity, speed, and low resource consumption in mind.

Unlike modern browsers such as Chromium and Firefox, Dillo has a small footprint and a relatively simple architecture. This makes it particularly interesting for **embedded Linux systems, ARM devices, older hardware, and resource-constrained platforms**.

## Features

- 🚀 Very fast startup
- 🪶 Extremely lightweight
- 💾 Low storage requirements
- 🧠 Low memory consumption
- ⚡ Low CPU usage
- 🐧 Designed for Unix/Linux systems
- 🖥️ Suitable for embedded Linux systems
- 🌐 Supports basic HTML and CSS
- 🔧 Simple architecture
- 📦 Suitable for minimal Linux distributions and Yocto-based systems

## Why Dillo?

Modern web browsers are powerful, but they also require substantial system resources.

Chromium and Firefox typically include large browser engines, extensive JavaScript runtimes, multimedia frameworks, graphics APIs, sandboxing infrastructure, and many other components.

Dillo takes a different approach.

It focuses on providing a **small and fast graphical web browser** without attempting to implement the entire modern web platform.

This makes Dillo particularly useful when the goal is to display a controlled web interface rather than browse the entire modern Internet.

## Embedded Linux

Dillo can be especially useful for embedded Linux applications.

For example:

```text
+-----------------------------+
|        Embedded UI          |
|          HTML/CSS           |
+-----------------------------+
             |
           Dillo
             |
          Wayland
             |
           Weston
             |
       Linux / ARM SoC
```

A system based on an ARM processor can use Dillo to display a locally hosted HTML interface while keeping memory and CPU requirements relatively low.

Potential applications include:

- Industrial HMIs
- Control panels
- Information displays
- Device configuration interfaces
- Kiosks
- Embedded dashboards
- Network device interfaces
- Local documentation
- Lightweight graphical frontends

## ARM and Embedded Systems

Dillo is particularly interesting on ARM-based embedded platforms where system resources are limited.

For example, on an embedded Linux platform such as the **STM32MP257F**, a possible software stack could look like:

```text
STM32MP257F
    │
    ├── Cortex-A35
    │
    ├── Linux
    │
    ├── Wayland
    │
    ├── Weston
    │
    └── Dillo
          │
          └── HTML / CSS
```

This can provide a lightweight graphical browser without the substantial resource requirements of a full Chromium-based solution.

## Advantages

### Low Resource Usage

Dillo is designed to use significantly fewer system resources than modern full-featured browsers.

This can be beneficial when running on:

- Embedded ARM processors
- Systems with limited RAM
- Small flash storage
- Older computers
- Minimal Linux distributions

### Fast Startup

Because Dillo is small and has considerably less functionality than modern browsers, it can start very quickly.

This is useful for applications where the browser is part of the user interface and should appear almost immediately after system boot.

### Small Footprint

Dillo can be considerably easier to integrate into a minimal Linux system than a large modern browser stack.

This is especially useful for embedded distributions built with systems such as Yocto.

### Simple Web Interfaces

If the application only needs to display a controlled HTML interface, Dillo can provide a practical solution without requiring the complete modern web platform.

## Disadvantages

Dillo's main advantage is also its biggest limitation: **it does not attempt to support the modern web to the same extent as Chromium or Firefox.**

### Limited JavaScript Support

Modern websites often depend heavily on JavaScript.

Complex web applications may therefore fail to work correctly or may not work at all.

Examples include many:

- Web applications
- Online office suites
- Modern social media sites
- Interactive dashboards
- Single-page applications

### Limited CSS Support

Modern websites frequently use advanced CSS features.

Complex layouts may therefore render incorrectly or appear very different from the original website.

### Limited Multimedia

Dillo is not intended to be a modern multimedia browser.

Websites relying on:

- HTML5 video
- Modern audio APIs
- DRM
- Streaming platforms

may not work as expected.

### Limited Web APIs

Many modern web technologies are outside Dillo's scope.

Examples include technologies such as:

- WebGL
- WebRTC
- Service Workers
- Modern browser APIs
- Advanced JavaScript APIs

As a result, Dillo should not be considered a general replacement for Chromium or Firefox.

## Dillo vs. Modern Browsers

| Feature | Dillo | Chromium / Firefox |
|---|---:|---:|
| Startup time | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| RAM usage | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| CPU usage | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| Storage footprint | ⭐⭐⭐⭐⭐ | ⭐ |
| Basic HTML | ✅ | ✅ |
| Basic CSS | ✅ | ✅ |
| Modern JavaScript | ❌ / Limited | ✅ |
| Modern CSS | ❌ / Limited | ✅ |
| WebGL | ❌ / Limited | ✅ |
| Modern Web APIs | ❌ / Limited | ✅ |
| Modern video streaming | ❌ / Limited | ✅ |
| Embedded systems | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| Modern websites | ⭐ | ⭐⭐⭐⭐⭐ |

## When Should You Use Dillo?

Dillo is a good choice when you need:

> **A graphical HTML interface with minimal system overhead.**

Typical use cases include:

```text
Embedded device
       │
       ▼
 Local web server
       │
       ▼
   HTML / CSS
       │
       ▼
     Dillo
       │
       ▼
     Display
```

It is particularly attractive when the web content is **under your control**.

For example, an embedded device could run a small local web server containing pages for:

- System configuration
- Hardware status
- Network configuration
- Diagnostics
- Device control
- Monitoring

In such a scenario, full modern browser functionality may not be necessary.

## When Should You NOT Use Dillo?

Dillo is not a good choice if the primary purpose is general Internet browsing.

Use a modern browser such as Chromium or Firefox when you need:

- Modern JavaScript
- WebGL
- YouTube and modern streaming
- Complex web applications
- Modern authentication systems
- Advanced HTML5 functionality
- Broad compatibility with today's websites

## Dillo on Yocto

Dillo can also be interesting for **Yocto-based embedded Linux distributions**.

A typical embedded image could contain:

```text
Yocto
 ├── Linux kernel
 ├── Wayland
 ├── Weston
 ├── Dillo
 ├── Local web server
 └── Application
```

This allows the browser to become part of a dedicated embedded graphical system rather than a general-purpose desktop environment.

## Philosophy

Dillo follows a fundamentally different philosophy from modern browsers.

Instead of trying to implement everything the modern web can do, it focuses on:

- Small size
- Speed
- Simplicity
- Low resource consumption
- Basic web rendering

That makes Dillo less suitable for the modern Internet, but potentially **very useful for embedded systems and controlled web interfaces**.

## Summary

Dillo is not intended to compete directly with Chromium or Firefox in terms of web compatibility.

Its strength is elsewhere:

> **Dillo provides a lightweight graphical web browser for systems where simplicity, speed, and low resource usage are more important than full modern web compatibility.**

For embedded ARM Linux systems, especially those running a minimal Yocto distribution, Dillo can therefore be an interesting alternative to much heavier browser engines.

---

## License

Please refer to the Dillo project's official repository and documentation for the current licensing information.

## Links

- Dillo Project: https://www.dillo.org/
- Dillo Source Code: https://github.com/dillo-browser/dillo
