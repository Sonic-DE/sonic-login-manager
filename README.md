## Sonic Login Manager

Sonic Login Manager provides a display manager for SonicDE, forked from [Plasma Login Manager](https://invent.kde.org/plasma/plasma-login-manager) based on [SDDM](https://github.com/sddm/sddm) and with a new frontend providing a greeter, wallpaper plugin integration, and System Settings module (KCM).

### What we want

 - Great out-of-box experience in multi-monitor
 - Keyboard layout switching
 - Virtual keyboards
 - Easy Chinese/Japanese/Korean/Vietnamese (CJK) input
 - Screen readers for blind people (which then means volume control)
 - Remote (VNC/RDP) support from startup
 - Deeper Plasma integration including:
    - Display and keyboard brightness control
    - Full power management
    - Pairing trusted Bluetooth devices
    - Login to known Wi-Fi for remote LDAP

### Getting started

To try Sonic Login Manager, you can build the repository and install it on your system.

> [!caution]
> It is not recommended to install this on your system — you should use a virtual machine instead. Installing this on real hardware will leave behind files not trivially uninstallable and could leave your system in a non-functional state.

You will need to:

- On Arch Linux, install `base-devel`, `git`, `cmake` and `extra-cmake-modules`
- Clone, build, and install:

```shell
git clone https://github.com/Sonic-DE/sonic-login-manager.git
cmake -S sonic-login-manager -B sonic-login-manager/build && sudo make install -C sonic-login-manager/build
```

- Trigger the system user to be created:

```shell
sudo systemd-sysusers
```

- Disable SDDM and enable Sonic Login Manager:

```shell
sudo systemctl disable sddm
sudo systemctl enable plasmalogin
```

- …and finally reboot.

### Configuration

Sonic Login Manager is configured by users through `/etc/plasmalogin.conf`, which overrides distro-provided defaults at `/usr/lib/plasmalogin/defaults.conf`. In managed scenarios, the latter file can be modified to set a default wallpaper or login session, with the settings module disabled via Kiosk.

