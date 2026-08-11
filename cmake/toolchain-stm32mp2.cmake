# STM32MP257F cross-compilation toolchain for OpenSTLinux (scarthgap, kernel 6.6).
#
# Targets aarch64 (Cortex-A35) using the OpenSTLinux SDK:
#   * compiler:  aarch64-ostl-linux-gcc / aarch64-ostl-linux-g++
#   * sysroot:   $SDKTARGETSYSROOT (set by the SDK environment script)
#
# Usage (after sourcing the SDK environment script):
#   source /path/to/environment-setup-cortexa35-ostl-linux
#   cmake -S . -B build-stm32mp2 -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-stm32mp2.cmake
#
# If you are not sourcing the environment script, pass -DSYSROOT=<path to the
# target usr root> explicitly.
#
# The SDK ships GLES (gcnano), Wayland (Weston) and GStreamer, which is exactly
# what the default GLES renderer needs. Vulkan on the STM32MP25 is still
# immature, so this toolchain forces the GLES backend.

if(NOT DEFINED SYSROOT)
    if(DEFINED ENV{SDKTARGETSYSROOT} AND NOT "$ENV{SDKTARGETSYSROOT}" STREQUAL "")
        set(SYSROOT "$ENV{SDKTARGETSYSROOT}")
    else()
        message(FATAL_ERROR
            "toolchain-stm32mp2.cmake: SDKTARGETSYSROOT is not set. "
            "Source the OpenSTLinux SDK environment script first, or pass -DSYSROOT=...")
    endif()
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER aarch64-ostl-linux-gcc)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER aarch64-ostl-linux-g++)
endif()

set(CMAKE_SYSROOT "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The Yocto SDK ships the pkg-config wrapper as aarch64-ostl-linux-pkg-config.
# Use it when available; otherwise point the host pkg-config at the sysroot so
# it still resolves the target's .pc files (GStreamer, GLES, libcurl, ...).
find_program(IMTUBE_SDK_PKG_CONFIG aarch64-ostl-linux-pkg-config)
if(IMTUBE_SDK_PKG_CONFIG)
    set(PKG_CONFIG_EXECUTABLE "${IMTUBE_SDK_PKG_CONFIG}" CACHE FILEPATH "Cross pkg-config" FORCE)
else()
    find_program(PKG_CONFIG_EXECUTABLE pkg-config)
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT}")
    set(ENV{PKG_CONFIG_LIBDIR}
        "${SYSROOT}/usr/lib/aarch64-ostl-linux/pkgconfig"
        "${SYSROOT}/usr/lib/pkgconfig"
        "${SYSROOT}/usr/share/pkgconfig"
    )
endif()

# --- ImTube options for the embedded target ---------------------------------
set(IMTUBE_RENDERER "gles" CACHE STRING "Rendering backend for STM32MP2 (GLES only)" FORCE)
set(IMTUBE_SDL_WAYLAND ON  CACHE BOOL "SDL3 with Wayland driver (Weston)" FORCE)
set(IMTUBE_EMBEDDED    ON  CACHE BOOL "Embedded target tweaks" FORCE)
set(IMTUBE_WITH_CURL   ON  CACHE BOOL "libcurl for thumbnails" FORCE)
