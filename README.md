## Sonic Login Manager

Sonic Login Manager provides a display manager for SonicDE, forked from [Plasma Login Manager](https://invent.kde.org/plasma/plasma-login-manager) based on [SDDM](https://github.com/sddm/sddm) and with a new frontend providing a greeter, wallpaper plugin integration, and System Settings module (KCM).

## Systemd

Systemd is a hard requirement for future plans, see 
https://invent.kde.org/plasma/plasma-login-manager/-/issues/48

### What we want

 - Run on X11
 - Init system agnostic: work on any init system
 - Great out-of-box experience in multi-monitor
 - Keyboard layout switching
 - Virtual keyboards
 - Easy Chinese/Japanese/Korean/Vietnamese (CJK) input
 - Screen readers for blind people (which then means volume control)
 - Remote (VNC/RDP) support from startup
 - Deeper SonicDE integration including:
    - Display and keyboard brightness control
    - Full power management
    - Pairing trusted Bluetooth devices
    - Login to known Wi-Fi for remote LDAP

### Important Note on Wayland Sessions

While  Sonic Login Manager runs under X11 only, it fully supports launching Wayland desktop environments (such as Plasma Wayland or Sway). Ensure your preferred Wayland compositor sessions are correctly populated in /usr/share/wayland-sessions/.

### Getting started

To try Sonic Login Manager, you can build the repository and install it on your system or use the packages for your distribution..

#### Building from source

You will need to:

- On Arch Linux, install `base-devel`, `git`, `cmake` and `extra-cmake-modules`
- Clone, build, and install:

```shell
git clone https://github.com/Sonic-DE/sonic-login-manager.git
cd sonic-login-manager
cmake -B build && sudo cmake --install build
```

#### Switching the Login Manager in your Init System

Disable SDDM (or whatever display manager you use) and enable Sonic Login Manager:

##### systemd

Switch the init system to use Sonic Login Manager:

```shell
sudo systemctl disable sddm
sudo systemctl enable soniclogin
```

##### bsd

```shell
sudo sysrc sddm_enable="NO"
sudo sysrc soniclogin_enable="YES"
```

##### dinit

```shell
sudo dinitctl disable sddm
sudo dinitctl enable soniclogin
```

##### OpenRC

```shell
sudo rc-update del sddm default
sudo rc-update add soniclogin default
```

##### runit

```shell
sudo rm /var/service/sddm
sudo ln -s /etc/sv/soniclogin /var/service/
```

##### S6

```shell
sudo s6 repository sync
sudo s6 set disable sddm
sudo s6 set enable soniclogin
sudo s6 set commit
sudo s6 live install
```

##### sysvinit

```shell
sudo update-rc.d sddm disable
sudo update-rc.d soniclogin defaults
sudo update-rc.d soniclogin enable
```

- …and finally reboot.

### Configuration

Sonic Login Manager is configured by users through `/etc/soniclogin.conf`, which overrides distro-provided defaults at `/usr/lib/soniclogin/defaults.conf`. In managed scenarios, the latter file can be modified to set a default wallpaper or login session, with the settings module disabled via Kiosk.
