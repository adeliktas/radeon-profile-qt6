# Radeon Profile (Qt6)

Linux GUI for monitoring AMD Radeon/amdgpu GPUs and controlling power profiles, clocks and fans. Version: **20261006**.

## Build

Requires CMake >= 3.22, a C++17 compiler, Qt6 Widgets/Network/Charts/Concurrent, libX11, libXrandr, libdrm headers and pkg-config. Qt6 LinguistTools is optional for building translations.

```sh
git clone https://github.com/adeliktas/radeon-profile-qt6.git
cd radeon-profile-qt6
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"
./build/radeon-profile
```

Install the GUI, desktop entry, icon and available translations:

```sh
sudo cmake --install build
```

The binary is **`build/radeon-profile`**. Prefer your distribution's package manager: manual installation bypasses Gentoo's package tracking and can be overwritten by emerge.

## Permissions and service managers

Monitoring does not require root or the daemon when DRM devices and sysfs readings are accessible. Ensure your account has access to the appropriate DRM render node (commonly via the `render` and/or `video` group).

Changing power, clocks or fan settings normally needs the separate [radeon-profile-daemon](https://github.com/blackPantherOS/radeon-profile-daemon). Install its service for your init system:

```sh
# OpenRC
sudo rc-service radeon-profile-daemon start
# Optional: start on boot
sudo rc-update add radeon-profile-daemon default

# systemd
sudo systemctl start radeon-profile-daemon.service
# Optional: start on boot
sudo systemctl enable radeon-profile-daemon.service
```

Alternatively, choose **General → Enable privileged controls…**. The GUI detects the running init system and invokes its service manager through `pkexec` to show the system authentication dialog. Cancelling leaves monitoring running. Install pkexec and a desktop polkit authentication agent for this feature. The GUI remains unprivileged; do not loosen sysfs permissions or run the entire desktop application as root.

This repository builds the GUI, not the daemon or its service files. Systems without OpenRC/systemd can still monitor GPUs and connect to a daemon started by their administrator.

## Fan curves

On the Fan Control tab, select a profile and check **Use hotspot (junction) temperature for this curve** to use the GPU's junction sensor instead of its edge sensor. This is saved per profile; older profiles keep using edge temperature. The checkbox is disabled if no readable junction sensor exists. Hotspot is also available in monitoring data.

Drag an existing graph point to adjust its temperature and fan speed; the table updates immediately. Points cannot cross neighbouring temperatures or make fan speed decrease along the curve. Supported points: 0–120°C and 0–100% fan speed.

Edits are marked unsaved. **Save**, then **Apply** to activate another profile. Saving an already active profile updates its control immediately. If the selected sensor becomes unreadable, automatic fan control is restored. Quit through the tray menu to restore automatic control; killing or crashing the GUI can leave the last manual fan setting active.

## Display information

GPU monitoring and fan control use DRM/sysfs, not X11. Detailed display/connector information still uses Xrandr and can be incomplete under Wayland/Xwayland. `glxinfo` and `xdriinfo` are optional diagnostic tools; their absence does not prevent GPU detection. Mesa's `DRI_PRIME` selection uses the GPU's PCI address rather than assuming card/render indices match.

## Checks

```sh
ctest --test-dir build --output-on-failure
# Hardware smoke check: AMD GPU, unprivileged user, daemon stopped
sh tests/monitoring.sh "$PWD/build/radeon-profile"
```

Tests cover command execution, service-manager selection on the running system, render-node discovery, hotspot discovery/read failures, fan interpolation and graph dragging.

Licensed under GPL-2. See `LICENSE`. Based on the original [radeon-profile](https://github.com/marazmista/radeon-profile) and the blackPantherOS Qt6 port.
