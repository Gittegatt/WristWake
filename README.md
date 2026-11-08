# WristWake

Alarm clock watchapp for the Pebble Time 2 (SDK platform `emery`) with up to 8 alarms and Home Assistant integration.

## About (Pebble App Store description)

I love waking up to the vibration of my Pebble on my wrist. The native Android alarm app can sync its alarms with Home Assistant through the Home Assistant Companion App. With a Pebble, however, there was no way to pass my alarms on to Home Assistant to trigger automations or other actions. WristWake bridges this gap.

With this alarm app, I wanted to:
- keep it intuitive and easy to use, close to the native Pebble alarm app
- sync my alarms with Home Assistant
- set alarms in bed without grabbing my phone
- make sure I really get up
- customize the colors

Features:
- up to 8 alarms with name, weekdays, snooze delay and protection against accidental deletion
- 18 ringtones, including 5 rising ones, each with 4 volume levels
- vibration patterns, with or without a ringtone
- snooze off: only holding the bottom button stops the alarm
- wake-up challenge: repeat a random button sequence (Follow) or guess it (Guess) before the alarm stops
- 10 color presets or your own colors, left-handed mode
- in English and German: German if your watch is set to German, English otherwise

In Home Assistant, your watch appears as its own device. It reports whether an alarm is ringing, snoozed or dismissed, and shows all alarms with their settings. This lets you, for example, turn on the lights or start the coffee machine when your alarm rings.

WristWake connects to your Home Assistant instance with a long-lived access token. The token is used only once to register your watch and is then discarded – it is never stored. After that, all updates are sent via webhook.

I designed the concept, the features and the look of this watchapp myself, down to the menu items, ringtones and vibration patterns. For the coding, I used Claude Code.

Coming next: an Android companion alarm app that enables two-way sync between Home Assistant, your Pebble and your Android phone.

## Run in the emulator

Both ways build the app and open it in the Pebble Time 2 emulator:

- **Windows Explorer:** double-click [`Start emulator.cmd`](Start%20emulator.cmd)
- **VS Code:** `Ctrl + Shift + B`

Keys in the emulator window: arrow up/down = Up/Down, arrow right = Select, arrow left = Back.
If the window cannot be brought to the front, run `wsl --shutdown` in PowerShell and start again.

**Settings page in the emulator:** double-click [`Open settings.cmd`](Open%20settings.cmd) while the emulator is running with WristWake open.

**On a real watch:** copy the `.pbw` file to the phone and open it with the Pebble app.

## Development (Windows with WSL/Ubuntu)

The Pebble SDK only runs on Linux, here in WSL (Ubuntu 24.04 or newer).

One-time setup (in Ubuntu):

```sh
sudo apt install build-essential nodejs npm libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0
curl -LsSf https://astral.sh/uv/install.sh | sh
uv tool install --python 3.13 pebble-tool   # pebble-tool needs Python 3.10-3.13
pebble sdk install latest
```

Build and run in the emulator (in the project folder):

```sh
pebble build
pebble install --emulator emery
```

Recreate the theme preview images of the settings page after changing the alarm list or menu (emulator running):

```sh
bash tools/make-previews.sh
```

## Structure

```
src/c/              watchapp C code
src/pkjs/           JavaScript part (runs in the Pebble app on the phone)
src/config/         settings page (bundled into the app)
resources/images/   app icon for the watch launcher
store/              icons and screenshots for the Pebble App Store
tools/              helper scripts (emulator, settings page, preview images)
package.json        project data (UUID, platforms, message keys)
wscript             build rules
```
