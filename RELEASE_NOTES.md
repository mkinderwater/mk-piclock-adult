# mk-clock-adult 2.3.54-preview59

## 2.3.54-preview59

- Added lightweight `/api/v1/assets/podcasts/summary` endpoint so the Unplayed count no longer waits for full podcast metadata parsing.
- Podcast Unplayed loads independently for large libraries and shows `Unavailable` on summary failure instead of remaining on `Loading...`.
- Summary refreshes independently after podcast playback, deletion, history reset, and importer activity.
- HTTP API advanced to v1.62.


## 2.3.54-preview58

- Make `/opt/mk-piclock/config/` an explicit persistent-data boundary during upgrades.
- Preserve clock settings, alarms, audio/display preferences, podcast play history, event log, and web password across normal upgrades.
- Preserve podcast/music/font inventories as before.
- Include the config directory in transactional rollback snapshots without treating it as release-owned content on successful deployment.

### 2.3.54-preview58
- Podcast bulk-import completion now reports processed, failed, and total counts.
- Failed podcast imports are listed by filename with their individual error reason; successful files are not listed.
- Added indoor temperature trend to the OLED INSIDE panel using framebuffer-drawn arrows, independent of the selected TTF font.
- The trend arrow occupies its own middle line: rising draws an up arrow, falling draws a down arrow, and stable temperature leaves the line blank.
- Relative humidity moved to Y=56, aligned with the small date baseline used by the neighboring forecast panels.
- Trend uses one-minute samples, compares smoothed five-sample averages, requires at least 20 minutes of history, and uses a 0.3 C deadband to suppress sensor noise.
- Podcast library cards no longer show per-item Played/Unplayed badges; play-cycle status remains summarized at library level.
- Reworked bulk podcast processing into one Music-style batch progress card.
- Shows one overall progress bar, current filename, current-file percentage, and batch position such as Processing 47 of 312.
- Hundreds of queued podcast files are represented by counts rather than individual progress rows.


- Podcast staging is operator-controlled: opening Podcasts does not scan or process uploads. **Scan uploads** performs the directory scan on demand; when files are found, **Process N podcasts** starts the one-at-a-time importer.
- Browser podcast uploads now stage files without auto-starting or auto-scanning, matching SSH/SFTP uploads.
- SSH/SFTP filenames with spaces and ordinary punctuation are accepted by the upload scan and normalized safely when promoted into the active library.

## Podcast random play history

- Random podcast playback is now random without replacement. Once a podcast has played, it is excluded from future random selections.
- Play history is persistent across reboots and upgrades in `/opt/mk-piclock/config/podcast-history.txt`.
- Manual podcast playback also marks the podcast as played, so the history represents what the listener has actually started.
- Playback is refused if the history marker cannot be saved, preserving the no-repeat guarantee.
- The Podcasts GUI shows `Unplayed X of Y` and marks every item as Played or Unplayed.
- `Reset play history` remains available to start a fresh cycle early without deleting podcast files.
- When all current podcasts have been played, the next random request automatically clears history and begins a fresh no-repeat cycle.
- Deleting a podcast removes its history entry. Re-uploading a filename also makes that podcast eligible again.
- Random selection uses kernel randomness with rejection sampling, falling back to the existing PRNG only if kernel randomness is unavailable.
- Private core/API IPC is v35. HTTP API remains v1.60.

## GUI consistency review

- Music and Podcasts now use matching introductory status-card structure and the same shared audio-library presentation.
- Podcast volume and the touch shortcut are separated into distinct setting cards, matching the focused card layout used elsewhere in the clock controls.
- Podcast upload guidance is user-facing and no longer exposes the transcoding implementation detail.
- The Music page now reports only music playback; an active podcast is no longer mislabeled as the current song.
- Dashboard audio summaries identify active podcast playback explicitly.


## Podcasts

- Added a separate Podcasts library for bedtime and background long-form MP3 playback.
- Podcast playback always restores/keeps the normal clock face; the podcast title replaces the date line while playing.
- Podcasts use their own volume setting and never enter the music/alarm selection pool.
- Ten short touch taps within eight seconds starts a random podcast when the shortcut is enabled; one short tap stops current audio.
- Podcast startup has a brief touch guard so the activation gesture cannot immediately stop the podcast.
- Browser controls support play, stop, delete, delete-all, and up to 14 MP3 uploads at once.
- Valid podcast MP3 files are stored directly with no transcoding pass, avoiding unnecessary CPU and storage I/O for long-form audio.
- Diagnostics report podcast storage separately. Music and podcasts remain excluded from configuration backups.
- HTTP API is v1.60. Private core/API IPC is v35.


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
- HTTP API remains v1.60. Private core/API IPC remains v35.
