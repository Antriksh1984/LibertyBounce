# LibertyBounce

Make classic **GTA III** bounce to your own music, or to the in-game radio.

Cars, boats, trains, helicopters, planes and people squash and stretch on every beat of a song you choose. Pick an MP3, FLAC, WAV or OGG in game; the importer detects the beat and loops the track while you play. Get in a car with the radio on and everything bounces to the station you're listening to instead.

LibertyBounce is a GTA III port of **[ViceBounce](https://www.moddb.com/mods/vicebounce)** for GTA Vice City.

## Videos

[![LibertyBounce video](https://img.youtube.com/vi/CFFwla8HTvg/hqdefault.jpg)](https://youtu.be/CFFwla8HTvg)

Watch it on YouTube: https://youtu.be/CFFwla8HTvg

[![LibertyBounce video 2](https://img.youtube.com/vi/FOkcpFPq97o/hqdefault.jpg)](https://youtu.be/FOkcpFPq97o)

Watch it on YouTube: https://youtu.be/FOkcpFPq97o

[![LibertyBounce video 3](https://img.youtube.com/vi/eSAAytorbWI/hqdefault.jpg)](https://youtu.be/eSAAytorbWI)

Watch it on YouTube: https://youtu.be/eSAAytorbWI

## Features

- Vehicles, pedestrians, the player and cutscene characters squash and stretch to the beat
- Import your own music in game (MP3, FLAC, WAV, OGG) with automatic beat detection
- Drivers and passengers bounce with their car; wheels and feet stay on the ground
- Purely visual: physics, collisions and missions are unaffected
- Separate toggles for cars and cutscenes, other vehicles, and people
- Adjustable tempo and bounce strength, saved automatically
- Same controls as ViceBounce, all remappable in the INI

### Additional features (new in LibertyBounce)

- **Bounces to the in-game radio:** in a car with the radio on, everything bounces to the station you're hearing, and switching station switches the beat
- **Talk detection:** DJ chat, adverts and Chatterbox calm the bounce down instead of jerking it around
- **Custom music on/off (F12):** turn your own song off to bounce only to the radio, or back on whenever you like
- **Trains bounce too**, along with boats, helicopters and planes

## Thank you, tek-95

LibertyBounce exists because of [@tek-95](https://github.com/tek-95), the author of ViceBounce. Huge thanks for making such a fun mod and for releasing it as open source. The bounce maths, beat detection, beat map, music playback and music importer all come from ViceBounce; this project rewrites the game hooks for GTA III and adds the radio features.

- ViceBounce on ModDB: https://www.moddb.com/mods/vicebounce
- ViceBounce on GitHub: https://github.com/tek-95/ViceBounce
- tek-95 on GitHub: https://github.com/tek-95

If you enjoy LibertyBounce, please go and star ViceBounce too.

## Download

Get `LibertyBounce-0.3.0.zip` from the [Releases page](../../releases) or from the [`releases`](releases) folder in this repository.

## Requirements

- GTA III for PC, **v1.0** executable. Other versions disable the mod and say so in `scripts\LibertyBounce.log`.
- An ASI loader that loads plugins from the `scripts` folder, such as [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- Windows 10/11.

## Install

1. Close the game.
2. Extract the zip and copy everything inside its `scripts` folder into your game's `scripts` folder: `LibertyBounce.asi`, `LibertyBounce.ini` and `LibertyBounceMusic.exe`.
3. Start the game. Vehicles bounce at 120 BPM straight away; press **F11** to pick your own song.

When updating, keep your existing `LibertyBounce.ini` so your song and beat alignment stay.

Tip: turn the in-game radio volume down (Options > Audio) so it doesn't clash with your track.

## Controls

The same keys as ViceBounce:

| Key | Action |
|---|---|
| F6 | Cars and cutscenes on/off |
| F7 | Reload `LibertyBounce.ini` |
| F8 | Boats, trains, helicopters and planes on/off |
| F9 | Line the squash up with the beat you hear |
| F10 | All people, including the player, on/off |
| F11 | Pick and import a song (not with Ctrl/Shift/Alt) |
| F12 | Custom music on/off (off = only the car radio drives the bounce) |
| Page Up / Page Down | Tempo +/- 5 BPM (turns off the detected beat map) |
| Home / End | Bounce strength +/- 0.025 |

Drivers and passengers always bounce with their vehicle. Changes made with hotkeys are saved to the INI.

## Bouncing to the in-game radio

Get in a car and turn the radio on: Liberty City bounces to whatever station you're listening to, and switching station switches the beat. Your own song pauses while the radio plays and comes back when you get out.

Prefer the radio only? Press **F12** to turn your custom music off: your song stops, the radio drives the bounce, and nothing bounces when no radio is playing. Press F12 again to bring your song back. (Radio following itself can be turned off with `Radio=0` in the INI.)

The first time you start the game, LibertyBounce analyses the nine radio stations in the background (about two minutes; the game stays playable and uses your own song or the fixed tempo until it's done). The results are saved in `scripts\LibertyBounceRadio\` and reused after that.

DJ talk, adverts and Chatterbox are recognised as talk, so the bounce calms down there instead of jerking around. Very fast music (drum & bass, some hip-hop) bounces on every other beat. If the bounce feels slightly early or late against the radio, press F9 on a beat; the correction is shared by all stations. Every key can be changed with the `Key...` lines in `LibertyBounce.ini` (Windows virtual-key codes).

GTA III also uses Home (camera) and Page Up/Page Down (sniper zoom) by default, just as Vice City does.

## How it works

Just before GTA III draws a vehicle or a ped (the `CEntity::Render` call at `0x474BD0`), LibertyBounce swaps the root RenderWare frame of its model for a squashed copy. Wheels, doors, limbs and weapons are child frames, so they follow. Cars and trains are shifted down so their wheels stay on the road; boats keep their waterline and aircraft their altitude.

Cutscene actors are different: their model's root sits at the scene's origin while the animation moves the body around. So, just before the scene is drawn, each cutscene actor is squashed around the centre of its own body parts, with its feet kept on the floor. Heads are separate objects in GTA III cutscenes and rebuild themselves from the body's head bone, so they follow the squash.

Every frame is restored at the 3D/2D boundary and again before `CGame::Process`, so physics, collision and missions never see the squash.

For the radio, LibertyBounce reads which station the game is streaming (`cMusicManager::m_nCurrentStreamedSound`) and how far into that station's file playback is (`cSampleManager::GetStreamedFilePosition`). Each station's beat map stores beat times plus a 0–1 strength: the share of the audio's energy below 200 Hz, which is high for music and close to zero for speech. Tempo is measured every two seconds, so each song on a station gets its own tempo.

## Known limitations

- Headlight and tail-light glows don't move with the squash.
- Props in cutscenes (bags, guns, phones) squash around their own centre, so a held prop can drift slightly from the hand on each beat.
- The MP3 player station isn't followed (it plays your own files, which can't be analysed in advance); classical music on Double Clef bounces only gently.

## Building from source

Requirements: Visual Studio 2022 Build Tools (C++), [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) with `Plugin_III.lib` built, and Python 3.12 for the importer.

1. Build plugin-sdk's GTA III library (`plugin_III\Plugin_III.vcxproj`, Release, Win32) and set `PLUGIN_SDK_DIR` to the SDK folder.
2. Run `Build.ps1`. It writes `bin\LibertyBounce.asi`.
3. Build the importer as described in [`importer/BUILD.md`](importer/BUILD.md) and put `LibertyBounceMusic.exe` in `bin\`.
4. Run `Package.ps1` to create `releases\LibertyBounce-<version>.zip`.

## Credits

- [@tek-95](https://github.com/tek-95) — Created [ViceBounce](https://github.com/tek-95/ViceBounce) ([ModDB](https://www.moddb.com/mods/vicebounce)), the mod LibertyBounce is ported from: bounce maths, beat detection, music playback and the music importer (MIT, see [`LICENSE-ViceBounce.txt`](LICENSE-ViceBounce.txt))
- [@DK22Pac](https://github.com/DK22Pac) — Created [plugin-sdk](https://github.com/DK22Pac/plugin-sdk), used for the GTA III hooks (zlib, see [`Plugin-SDK-LICENSE.txt`](Plugin-SDK-LICENSE.txt))
- Third-party libraries bundled in the importer are listed in [`ThirdParty/NOTICE.txt`](ThirdParty/NOTICE.txt)

## Licence

LibertyBounce is released under the MIT licence; see [`LICENSE`](LICENSE). No music, game files or audio caches are included.
