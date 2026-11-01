# Sonic Login Manager Release Notes

Sonic Login Manager is the SonicDE login screen: it greets you when your computer starts, signs you in, and has a System Settings page for choosing its look, wallpaper and automatic login.

## Unreleased

- The login screen, its settings page and the sign-in helper now talk to each other through SonicDE's new shared messaging system, which is faster and more reliable. Restart the login manager after upgrading.
- Logs now go through SonicDE's new logging system: to the system journal where one is available, otherwise to `/var/log/sonic`, and old log files are cleaned up automatically.
- The OpenRC, dinit, runit and s6 service files now prepare the log folder themselves and rotate logs correctly.
- The login manager can now be started on systems that use sysvinit, and installing it on an OpenRC system no longer risks replacing its OpenRC start-up script.
- The login screen now uses the Silver Dark theme by default, so text and icons stay light and readable over the wallpaper, and all text follows your theme's colors.
- The theme you pick in System Settings is now applied properly, both when you click "Apply desktop settings" and when the login screen styles itself.
- Copying your theme to the login screen is quicker and keeps System Settings responsive, since themes already installed system-wide are no longer copied.
- The login screen uses the SonicDE on-screen keyboard, and your picture and password field stay visible and centered above it while it is open.
- Your chosen mouse cursor now shows up on the login screen on every system, and cursor handling is more dependable.
- The "Picture of the Day" wallpaper now works out of the box.
- Fixed the blur around the login prompt being cut off at the edges.
- Behind the scenes: new automated tests make sure theme and wallpaper settings keep being applied correctly.

## 6.8.0.3 (2026-10-25)

- The login screen now uses the Silver Dark theme by default, so text and icons stay light and readable over the wallpaper.
- All text on the login screen now follows your theme's text colors.
- The theme you pick in System Settings is now applied properly and used to style the login screen.
- Copying your theme to the login screen is quicker and keeps System Settings responsive, since themes already installed system-wide are no longer copied.
- Fixed problems with the mouse cursor on the login screen.

## 6.8.0.2 (2026-10-23)

- The login screen now uses the SonicDE on-screen keyboard.
- Text and icons on the login screen now adapt their color to stay readable against the background.
- The "Picture of the Day" wallpaper now works out of the box.
- Clicking "Apply desktop settings" in System Settings now carries your theme over to the login screen correctly.
- Your chosen mouse cursor now shows up on the login screen on systemd-based systems too.
- Fixed the blur around the login prompt being cut off at the edges.
- Behind the scenes: new automated tests make sure theme and wallpaper settings keep being applied correctly.

## 6.8.0.1 (2026-10-21)

- When the on-screen keyboard is open, your picture and password field stay visible and sit neatly centered above it.

## 6.8.0 (2026-10-20)

- Fixed the drop shadow behind the clock on the login screen.
- Signing in no longer fails when your personal state folder is missing; it is now created automatically.
- Gentoo systems now get the correct sign-in configuration for whichever init system they use.
- Distributions can choose which init system service files get installed.
- Behind the scenes: dependency updates and release preparation.

## 6.7.5.3 (2026-10-01)

- Signing in no longer fails when your personal state folder is missing; it is now created automatically.

## 6.7.5.2 (2026-09-21)

- Gentoo systems now get the correct sign-in configuration for whichever init system they use.

## 6.7.5.1 (2026-09-21)

- Distributions can choose which init system service files get installed.

## 6.7.5 (2026-09-12)

- The login manager no longer needs any Plasma or Qt 5 libraries.
- The login screen now uses SonicDE's own window manager, Sonic Win.
- Wallpapers on the login screen now appear reliably on every monitor, even when screens take a moment to come online.
- Fixed wallpaper changes from System Settings sometimes not taking effect.
- Fixed several crashes when logging out or when the login screen shuts down.
- The login manager now finds the display server reliably on more distributions.
- The login screen wallpaper service on systemd systems now uses the correct name.
- Distributions can install their own init system service files instead of relying on automatic detection.
- Behind the scenes: fixed the settings tests, release preparation.

## 6.7.4.8 (2026-10-01)

- Signing in no longer fails when your personal state folder is missing; it is now created automatically.

## 6.7.4.7 (2026-09-21)

- Gentoo systems now get the correct sign-in configuration for whichever init system they use.

## 6.7.4.6 (2026-09-21)

- The login screen now uses SonicDE's own window manager, Sonic Win.
- Wallpapers on the login screen now appear reliably on every monitor, even when screens take a moment to come online.
- Fixed wallpaper changes from System Settings sometimes not taking effect.
- Fixed several crashes when logging out or when the login screen shuts down.
- The login manager now finds the display server reliably on more distributions.
- The login screen wallpaper service on systemd systems now uses the correct name.
- Distributions can choose which init system service files get installed, or install their own.
- Behind the scenes: fixed the settings tests.

## 6.7.4.5 (2026-08-30)

- Fixed several crashes when logging out or when the login screen shuts down.
- The login screen wallpaper service on systemd systems now uses the correct name.

## 6.7.4.4 (2026-08-20)

- Distributions can install their own init system service files instead of relying on automatic detection.

## 6.7.4.3 (2026-08-18)

- The login manager now finds the display server reliably on more distributions.

## 6.7.4.2 (2026-08-13)

- Wallpapers on the login screen now appear reliably on every monitor, even when screens take a moment to come online.
- Fixed wallpaper changes from System Settings sometimes not taking effect.
- Behind the scenes: fixed the settings tests.

## 6.7.4.1 (2026-08-12)

- The login screen now uses SonicDE's own window manager, Sonic Win.

## 6.7.4 (2026-08-03)

- The login screen now uses SonicDE's Silver look instead of Breeze.
- Gentoo is now supported out of the box with its own sign-in configuration.
- The login manager now finds the display server on most distributions.
- Log files now live in clearer places with safer permissions: the system log at `/var/log/sonic-login-manager.log`, and the settings page log in your own `~/.local/state/sonic` folder.
- Behind the scenes: code style settings now follow the shared SonicDE rules.

## 6.7.3.6 (2026-10-01)

- Signing in no longer fails when your personal state folder is missing; it is now created automatically.

## 6.7.3.5 (2026-09-21)

- Gentoo systems now get the correct sign-in configuration for whichever init system they use.

## 6.7.3.4 (2026-09-21)

- The login screen now uses SonicDE's Silver look instead of Breeze, and SonicDE's own window manager, Sonic Win.
- Gentoo is now supported out of the box with its own sign-in configuration.
- The login manager now finds the display server reliably on most distributions.
- Log files now live in clearer places with safer permissions: the system log at `/var/log/sonic-login-manager.log`, and the settings page log in your own `~/.local/state/sonic` folder.
- Wallpapers on the login screen now appear reliably on every monitor, even when screens take a moment to come online.
- Fixed wallpaper changes from System Settings sometimes not taking effect.
- Fixed several crashes when logging out or when the login screen shuts down.
- The login screen wallpaper service on systemd systems now uses the correct name.
- Distributions can choose which init system service files get installed, or install their own.
- Behind the scenes: shared code style settings and fixed settings tests.

## 6.7.3.3 (2026-08-01)

- The login screen now uses SonicDE's Silver look instead of Breeze.
- Behind the scenes: code style settings now follow the shared SonicDE rules.

## 6.7.3.2 (2026-07-30)

- Gentoo is now supported out of the box with its own sign-in configuration.
- The login manager now finds the display server on most distributions.

## 6.7.3.1 (2026-07-30)

- Log files now live in clearer places with safer permissions: the system log at `/var/log/sonic-login-manager.log`, and the settings page log in your own `~/.local/state/sonic` folder.

## 6.7.3 (2026-07-14)

- Behind the scenes: restored and extended the code style rules.

## 6.7.2.4 (2026-07-10)

- The password prompt in the login screen settings now looks like the other password prompts on your desktop.

## 6.7.2.3 (2026-07-10)

- Fixed log files sometimes being created with the wrong permissions on first start.

## 6.7.2.2 (2026-07-05)

- Your desktop session no longer inherits special privileges from the login manager, making sign-in more secure.
- Behind the scenes: tidier diagnostic logging.

## 6.7.2.1 (2026-07-04)

- The login screen settings page now appears correctly on systems using dinit.

## 6.7.2 (2026-07-02)

- Your mouse cursor theme now shows up on the login screen.
- Screen settings you choose are now carried over to the login screen correctly.
- Plugging in or turning on several monitors at once is handled more smoothly at startup.
- systemd is no longer required to build or run the login manager.
- Behind the scenes: log messages now use the right severity levels.

## 6.7.1.2 (2026-06-28)

- Automatic login now works on systems without systemd.
- Cursor themes you installed yourself are now available on the login screen too.

## 6.7.1.1 (2026-06-28)

- Settings from Plasma Login Manager are imported automatically the first time you start Sonic Login Manager.
- Changes in the login screen settings page now reach the login manager through a faster, more reliable connection.
- Fixed the login screen showing a default system wallpaper instead of the one you picked.
- Fixed wallpaper choices sometimes getting lost when changing them quickly.
- Wallpaper settings are now saved correctly whichever wallpaper type you use.
- "Picture of the Day" is only used as a fallback when no other wallpaper is set.
- Password prompts in the login screen settings now place the cursor in the password field straight away.
- Settings files now use the correct names.
- Behind the scenes: removed setup files that are no longer needed and added more diagnostic logging.

## 6.7.1 (2026-06-23)

- The login manager now prepares a few more folders for its own account on first start, so features and settings keep working later.

## 6.7.0 (2026-06-20)

- Behind the scenes: build and code cleanups and a clearer diagnostic message.

## 6.6.5.6 (2026-06-06)

- If the login manager can't finish setting itself up on first start, it now stops with a clear error message instead of carrying on half-broken.
- Behind the scenes: build cleanups, including the BSD build.

## 6.6.5.5 (2026-06-05)

- The display server now runs without administrator rights on every system while switching between text consoles keeps working.
- The login manager now creates and sets up its own account automatically on systemd systems as well.
- Its account's home folder is now handled more reliably.
- Display server logs are now kept in the login manager's own folder for the login screen and in your home folder for your session.
- Logging works better with every init system, including when your service manager collects the output itself.
- Behind the scenes: cleaner logging, plus BSD-specific adjustments.

## 6.6.5.4 (2026-06-01)

- Fixed automatic login on Artix Linux.
- Fixed the automatic login setting not being saved.
- On OpenRC systems, the login screen now waits briefly for graphics drivers to be ready before starting.
- Sign-in configuration is now installed in the standard location.
- Behind the scenes: clearer error reporting when the session helper fails to start, and a comment fix.

## 6.6.5.3 (2026-05-31)

- Plasma Login Manager is now Sonic Login Manager, with new names throughout.
- The README now explains setup for any init system.

## 6.6.5.2 (2026-05-30)

- The login manager now sets up its own account and folders automatically on first start, instead of relying on each init system's service files.
- Improved support for runit, s6 and dinit, including starting on the right text console.
- The BSD service script now runs the login manager properly in the background.
- Fixed a crash in the display server helper and better handling when the display server quits unexpectedly.
- Your session is now properly registered with the system, so session tracking and permissions work as expected.
- The folder your session needs for temporary runtime files is created if it is missing.
- The login screen settings page now appears on every init system, not only systemd.
- Logging now goes to the system journal only when it is actually running.
- On BSD, the login manager account is now added to the groups it needs.
- Behind the scenes: reorganized init system files to prepare for separate packages per init system, a reworked logger, more helpful diagnostic messages and code cleanups.

## 6.6.5.1 (2026-05-21)

- Logs now go to the new SonicDE log location on every init system.
- The login manager now finds its sign-in configuration in more places and copes better when it's missing.
- Switching between text consoles now works correctly on systems without elogind.

## 6.6.5 (2026-05-17)

- Added support for OpenRC, dinit, runit and s6, with better automatic detection of your init system.
- The login manager no longer switches text consoles when it's already on the right one.
- Display server logs now only contain errors and warnings, so they're easier to read.
- The README now uses the Sonic Login Manager name.
- Behind the scenes: logging cleanups and a BSD build fix.

## 6.6.4 (2026-05-04)

- Shut down and restart now work from your session on systems using ConsoleKit.
- The login screen has its own power and session buttons that no longer depend on Plasma.
- Added FreeBSD and other BSD support, including start-up scripts, the correct sign-in configuration and text console handling.
- The login manager now works without systemd: service files are only installed where systemd exists, and a session message bus is started for you when needed.
- The display server now runs as an unprivileged user.
- Sign-in configuration now matches the proven setup used by SDDM.
- Fixed the login screen freezing at a password prompt.
- Fixed sign-in sometimes starting before the helper was ready.
- The login screen account is now added to the groups a login manager normally needs.
- Logs now go to both a log file and the system log, with clearer messages when something goes wrong.
- Behind the scenes: build fixes, dropped an unused dependency, compiler warning fixes, code cleanups and more diagnostic logging.

## 6.6.3 (2026-04-06)

- The login manager now runs on X11 and no longer requires systemd.
- Added sign-in configuration for FreeBSD and GhostBSD, and Artix Linux now works with elogind.
- The display server now starts before the login screen, and the login screen stays above the wallpaper.
- The login screen falls back to your desktop wallpaper, or to "Picture of the Day", when no login wallpaper is set.
- Login works even when some session folders and settings aren't set up.
- Clearer messages in the logs when something goes wrong.
- Behind the scenes: removed leftover Wayland code, fixed build warnings and errors, and code cleanups.
