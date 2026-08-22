# mk-clock-adult 2.3.50-preview42

## Weather reboot and offline cache recovery

- Cached weather restoration no longer waits for `network-online.target`; it starts as soon as the clock core and local API are started.
- `/run/mk-piclock/weather.json` is no longer used as evidence that the current core process received weather.
- A dedicated `/run/mk-piclock/weather-published.stamp` is written only after the core acknowledges a weather update.
- The publication stamp is compared with `/run/mk-piclock/core.sock`, so a core restart or software upgrade automatically republishes the valid last-good forecast even when runtime JSON survived.
- Cached weather publication retries the local API/core path during startup, covering the short race while the API and core socket become ready.
- Runtime JSON and the persistent last-good cache are now written only after the core has accepted the corresponding weather state.
- Runtime icon files are snapshotted before publication and rolled back if the core rejects the update, preventing failed refreshes from pairing new icons with old in-memory weather.
- With a valid cache less than 24 hours old, loss of Internet after a reboot or application restart should therefore show cached panels instead of leaving the OLED on `?` placeholders.

## Serialized music upload and transcoding

- Music uploads remain limited to 14 MP3 files per batch.
- The internal job table is now 14 slots, matching the maximum batch size.
- Only one music upload batch may be received at a time. A competing upload is rejected before its request body is written to temporary storage.
- After the full batch is staged, transcoding begins and remains strictly one song at a time.
- New music uploads are rejected while any song is queued or processing, avoiding upload I/O competing with transcoding CPU and SD-card I/O.

## Touch power wiring documentation

- Installer preflight now shows the complete TTP223B wiring: VCC to 3.3 V on physical pin 17, GND to physical pin 39, and OUT to PA17 on physical pin 37.
- README touch wiring now shows power, ground, and signal together instead of only the signal pin.
- `pinouts.md` already carried the correct electrical wiring and remains authoritative.

## Font upload selection

- Uploading a TTF or OTF font now adds it to the available font list without changing the active clock font.
- The active clock font changes only when the user selects a font in Clock display and saves the setting.
- If an uploaded file replaces a font that is already selected, the font cache is refreshed so the existing selection can use the updated file.
- The Display page now states that an uploaded font must be chosen manually before it is used.

## Previous fixes retained

- Preview37 weather high-time fallback remains intact: a valid derived time is shown when available; otherwise an official high is labelled `HIGH`.
- Preview36 documentation, wiring, and playback-only cleanup remains intact.
- Preview35 compile hotfix remains intact.

## Playback-only hardware contract

- Paired base: `bpi-zero-clock 1.0.4-preview36`.
- Kernel: `6.12.101+deb13-armmp`.
- MAX98357A playback: PA19/BCLK pin 27, PA18/LRC pin 28, PA20/DIN pin 40, PA1/SD-EN pin 11.
- Touch: VCC 3.3 V pin 17; GND pin 39; OUT PA17 pin 37 / gpiochip0 line 17.
- PA21 / physical pin 38: free / unassigned.
- HTTP API remains v1.59. Private core/API IPC remains v33.
