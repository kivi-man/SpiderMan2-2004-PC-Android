# Spider-Man 2 (Xbox) — Android install guide

**English** · [Türkçe](ANDROID.tr.md)

This guide takes the Android build made from your own `default.xbe`, installs it on a phone and gets the game running.

> [!IMPORTANT]
> The APK contains code made from your copy of the game. **Don't share it or upload it anywhere.** The game files are
> not in the APK either; you put your own copy on the phone yourself.

> [!NOTE]
> This build is experimental. It has been tested in game on a phone with a Mali-G610 (Redmi Note 13 Pro+) and in
> the Android emulator. Saving, loading and long play have not been tried yet.

## What you need

| | |
|---|---|
| **Phone** | Android 8.0 or newer with a 64-bit (ARM64) processor: nearly every phone of the last 6-7 years. |
| **Graphics** | OpenGL ES 3.1 or newer (3.2 recommended). Adreno (Snapdragon), Mali and Xclipse GPUs work. |
| **Memory** | 4 GB of RAM or more recommended. |
| **Storage** | About 1.7 GB for the game, plus ~200 MB for the app and its cache. |
| **Game** | Your own *Spider-Man 2* for the **original Xbox**, whose `default.xbe` has the SHA-1 `853bcb253c5baea2d7e2d9b59e5888b4c87bb402` (the US release). |
| **APK** | You build it yourself: [Building the APK](#building-the-apk-on-a-computer). No ready-made APK is distributed, since it contains code made from the game. |

## 1. Install the APK

1. Connect the phone to the computer with a USB cable and pick **File transfer** (MTP) in the notification on the phone.
2. Copy the APK into the phone's **Download** folder.
3. Open the **Files** app on the phone and tap the APK.
4. If an "unknown apps" warning comes up, tap **Settings** and turn on **Allow from this source** for the files app.
   Go back and tap **Install**.
5. If Play Protect warns about it, choose **Install anyway**. That is normal for an unsigned test build.

> With developer options and USB debugging on, one command from the computer installs it:
>
> ```bash
> adb install -r android/app/build/outputs/apk/debug/app-debug.apk
> ```

## 2. Open the app once

Tap **Spider-Man 2** on the home screen. The first time, with no game there yet, it says:

> *Game not found — Put your own copy of Spider-Man 2 (original Xbox) in …/files/game*

That is expected: the app has made its folder. Tap **OK** to close it.

## 3. Copy the game to the phone

The game goes in:

```
Phone storage / Android / data / com.spiderman2.xbox / files / game /
```

1. With the phone connected over USB, open its storage in File Explorer on the computer.
2. Go into `Android` → `data` → `com.spiderman2.xbox` → `files` → `game`.
3. Copy the game there, one of:
   - **A disc image (recommended):** the `.iso`, `.xiso`, `.cci` or `.cso` file itself, for example
     `Spider-Man 2 (World).xiso.iso`. Its name doesn't matter.
   - **The disc's files:** `default.xbe` with the `data` folder beside it (`data\game.ini`, `data\packs\…`, `data\sound\…`, `data\movies\…`).
4. Wait for the copy to finish (1.7 GB, 1-3 minutes depending on the cable).

> [!TIP]
> **Can't see `Android/data`?** Android 11 and newer hide it from the phone's own files app, but it usually shows
> **from a computer over USB**. If it doesn't, unplug and plug the phone back in, or keep its screen unlocked.
>
> **Don't use `adb push`:** a file copied there with `adb` isn't owned by the app, which then can't read it ("Game not
> found"). Copy it with USB file transfer (MTP).

## 4. Play

Open the app again. After a short loading screen come the game's intro movies and **PRESS START TO BEGIN**. The phone
turns to landscape. The first time, close Android's "Viewing full screen" notice with **Got it**.

The first minutes may stutter briefly: the game builds each effect's shader the first time it sees it. They are kept
in `cache/shaders.cache` and built ahead at the next start.

## Controls

### Touch

| On the screen | Xbox button | In game |
|---|---|---|
| Stick at the bottom left (appears where your thumb lands) | Left stick | Walk / run, menu navigation |
| Drag anywhere free on the right half | Right stick | Camera |
| **A** | A | Jump, select |
| **B** | B | Back, stick to walls |
| **X** | X | Punch, delete in menus |
| **Y** | Y | Confirm in menus (Enter) |
| **RT** (top right) | Right trigger | Web swing |
| **LT** (top left) | Left trigger | Sprint |
| **BLK** / **WHT** | Black / White | |
| **START** / **BACK** (top middle) | Start / Back | Pause, map |
| The phone's back button or gesture | Start | Pause |

The stick appears where your thumb lands on the left half and its knob follows your finger. Pressed buttons light up.
The camera follows a drag 1:1: the game's stick threshold and acceleration are turned off while you drag.

### Sensitivity and size: `controls.ini`

The first start writes `files/controls.ini`. Edit it from a computer and start the game again:

| Setting | Default | What it does |
|---|---|---|
| `camera_sensitivity` | 1.0 | Camera speed. 1 = a drag across the screen's width turns about half around. 2 twice that, 0.5 half. |
| `invert_y` | 0 | 1: dragging up looks down |
| `size` | 1.0 | Size of the buttons and the stick (0.7 – 1.5) |
| `opacity` | 1.0 | How visible the buttons are (0.3 – 1.5) |

### Controllers

Xbox, PlayStation and Switch Pro controllers work over Bluetooth or USB. Pressing a controller button hides the touch
controls; touching the screen brings them back.

## Files

All under `Android/data/com.spiderman2.xbox/files/`:

| Folder | What |
|---|---|
| `game/` | Your copy of the game |
| `save/progress/` | Saved games. **Back it up to a computer before uninstalling the app**: Android deletes this folder with it. |
| `save/data/` | The game's own data on the Xbox's hard disk |
| `cache/` | The shader list and the rest of the Xbox's hard disk; safe to delete |
| `logs/` | `sm2.log`: where to look when something goes wrong |

## Updating

Install the new APK over the old one. The `game/` and `save/` folders stay.

## Troubleshooting

| Symptom | Fix |
|---|---|
| **"Game not found"** | Is the file in `files/game/`? Is it `.iso`, `.xiso`, `.cci` or `.cso`? If you copied it with `adb`, copy it again over USB file transfer. |
| **"Another release of the game"** | Your copy isn't the supported US release. The APK takes exactly the game its `default.xbe` was built from. |
| **"OpenGL ES 3.1 is needed"** | The phone's GPU is too old; this build doesn't run on it. |
| **The app closes at once** | Look at the end of the newest file in `logs/`; the lines starting with `error:` tell what went wrong. |
| **Black screen or closing after coming back from the background** | Known issue: close the app fully and open it again. Save at the save points. |
| **Broken or missing graphics** | Note the phone's GPU model and keep the `logs/` file. GPU drivers differ. |
| **Slow** | Plug the phone in and turn battery saver off. The stutters in the first minutes are shader builds and pass. |

## Building the APK (on a computer)

1. The Windows version must have been built once: that makes the game's code in `generated/game` from
   `assets/default.xbe` ([README](../README.md#windows)).
2. You need the Android SDK, NDK 28.2.13676358 and JDK 17 or 21. `android/local.properties` holds the SDK's path
   (`sdk.dir=C:/Users/<user>/AppData/Local/Android/Sdk`).
3. In the `android` folder:

   ```bash
   gradlew.bat assembleDebug -PABIS=arm64-v8a
   ```

   `arm64-v8a` alone for phones; `-PABIS=arm64-v8a,x86_64` for the emulator too. The first build takes 5-10 minutes.
4. The result: `android/app/build/outputs/apk/debug/app-debug.apk`.
