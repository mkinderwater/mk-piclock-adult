# mk-clock-adult 2.3.54 user manual

This manual explains how to use the clock after installation.

For installation, wiring, or hardware migration, use `INSTALL.md`, `pinouts.md`, and `HARDWARE_MIGRATION.md`.

## 1. What the clock does

The clock is designed to remain useful without constant interaction.

Normal operation provides:

- time and date on the OLED
- three configurable weather panels
- indoor temperature and humidity from the AHT10 sensor
- an indoor temperature rising/falling indicator
- up to seven alarms
- music playback
- separate Podcast playback for sleep/background listening
- bedtime display dimming
- a touch button for common bedside actions
- browser controls for setup and management
- local weather caching so temporary Internet loss does not immediately blank the weather display

The web GUI is the main setup interface. The touch button handles the actions that make sense from bed without opening a browser.

## 2. Daily use

For normal use, the clock can be left on the clock screen.

Use the touch button as follows:

| Action | Result |
| --- | --- |
| Short tap while audio is playing | Stop the current music, podcast, or alarm |
| Hold about 3 seconds | Start random Music |
| 10 short taps within 8 seconds | Start one random unplayed Podcast, if the shortcut is enabled |
| Hold 15 seconds | Open OLED network/system diagnostics |
| Touch while diagnostics are open | Close diagnostics |

Podcast playback has a 1-second guard after the ten-tap gesture so the final taps do not immediately stop the podcast.

### Why the gestures are different

A long press starts Music because it is easy to perform intentionally.

Podcasts use ten taps because Podcast playback is intended as a less frequent sleep/background action and should not start from an accidental touch.

The 15-second diagnostics hold is deliberately much longer so normal bedside use cannot open diagnostics by mistake.

## 3. The OLED screen

The OLED is divided into the clock area and three weather panels.

The clock continues showing time during normal Music and Podcast playback.

### Podcast playback

When a Podcast is playing:

- the clock remains in clock mode
- the Podcast title replaces the normal date line
- long titles scroll as required
- the Podcast uses its own volume setting

The clock does not enter a special full-screen Podcast display. This is intentional. Podcast mode is designed for listening while falling asleep without losing the bedside clock.

### Screen off

**Screen off** clears the OLED without shutting down the clock.

Alarms, weather, networking, and services continue operating.

Use **Return to clock** to restore the normal display.

## 4. Indoor temperature panel

Any weather panel can be set to **Inside sensor**.

The panel uses the local AHT10 sensor and does not depend on Internet weather.

It shows:

- indoor temperature
- temperature trend when meaningful
- relative humidity on the lower line

### Temperature trend

The trend symbol is drawn by the clock itself. It does not depend on the selected font.

The clock samples the trend once per minute and compares smoothed 3-minute averages about 10 minutes apart.

An up arrow appears when temperature has risen by at least about `0.15 C`.

A down arrow appears when temperature has fallen by at least about `0.15 C`.

Once an arrow appears, hysteresis keeps it active until movement returns inside about `+/-0.08 C`. This prevents the arrow from rapidly appearing and disappearing around the threshold.

When there is no useful trend:

- no arrow is shown
- the temperature returns to its original centered position

When a trend exists:

- temperature moves upward
- the arrow receives its own middle line
- RH remains on the bottom line

Trend history is held in RAM. After a restart, the arrow remains blank until enough new readings exist to establish a trend.

## 5. Opening the web controls

Open the clock in a browser using its hostname or IP address.

The hostname is shown under **System > Network** and can be changed under **System > Hostname**.

The GUI contains:

- Dashboard
- Alarms
- Music
- Podcasts
- Weather
- Display
- System
- Recent activity

Use the web GUI for configuration. Use the touch button for simple bedside actions.

## 6. Dashboard

Dashboard is the first place to check when you want to know what the clock is doing.

### What the clock is showing

The OLED preview mirrors the physical OLED.

Use it when changing display, weather, or audio settings so you can see the current clock state without standing beside the clock.

### At a glance

The summary tiles show:

- Clock state
- Audio state
- Next alarm
- Bedtime status
- Inside sensor
- Weather status

### Quick actions

**Return to clock** restores the normal OLED clock display.

**Play music** starts Music playback.

**Screen off** clears the OLED only. It does not stop services or disable alarms.

**Stop / dismiss** stops current audio or dismisses the active alarm.

### Clock details

Expand **Clock details** when checking:

- current time/date
- clock name
- current audio
- volume
- next alarm
- last successful alarm
- bedtime schedule
- indoor sensor
- current weather
- forecast state

Expand **Support info** for software version, uptime, display mode, screen colour, touch state, and font information.

### NTP warning

If Dashboard says the clock is not synchronized, treat alarm times as unreliable until NTP is healthy.

Check **System > Network** and **System > NTP source**.

## 7. Alarms

The clock provides up to seven alarm slots.

Each alarm has its own:

- On/Off state
- time
- repeat days
- wake-up music choice
- starting volume
- final volume

### Setting an alarm

1. Open **Alarms**.
2. Choose **On**.
3. Set the time.
4. Select the days the alarm should run.
5. Choose a specific song or leave the music choice at **Choose a random song**.
6. Set starting and final volume.
7. Select **Save alarm**.

### Repeat days

Only selected days are eligible.

For a weekday alarm, select Monday through Friday.

For a one-day schedule, select only that day and turn the alarm off after it has been used if you do not want it to repeat the following week.

### Random versus fixed wake-up music

**Choose a random song** lets the clock select from the Music library when the alarm fires.

Choosing a filename pins that alarm to a specific song.

If that song is later deleted, the alarm returns to Random rather than retaining a broken file reference.

### Alarm volume ramp

Starting and final volume are absolute values from 0 to 100.

The clock ramps from the starting value toward the final value while the alarm audio plays.

This is independent of normal Music volume. It exists so an alarm can begin quietly and become louder without changing daytime Music settings.

An alarm can run for a maximum of 30 minutes unless dismissed first.

### Dismissing an alarm

Use either:

- one short touch on the clock
- **Stop / dismiss** on Dashboard

## 8. Music

Music serves two purposes:

- normal playback
- alarm audio library

Podcast files are separate and never appear as alarm songs.

### Music volume

**Normal volume** controls Music playback.

Changing and saving it also updates a normal song that is already playing.

Alarm start/final volumes remain independent.

### Adding Music

The browser accepts up to 14 MP3 files per batch.

The sequence is:

1. choose files
2. choose quality settings if required
3. select **Add music**
4. wait for the upload to finish
5. the clock prepares songs one at a time

Do not start another Music upload until the current processing queue finishes.

### Music quality settings

The defaults are intended for the clock's mono speaker.

**File quality** changes MP3 bitrate.

- 64 kbps: smallest files
- 96 kbps: recommended
- 128 kbps: higher quality
- 160 kbps: highest offered quality

**Sample rate** selects 32 kHz or 44.1 kHz.

44.1 kHz is the normal choice.

**Frequency range** applies the low-pass limit.

16 kHz is the normal choice for this speaker.

Use the recommended values unless you have a specific reason to change them. Higher settings consume more storage without necessarily improving the small mono speaker.

### Preparing Music

Music is transcoded one file at a time.

The progress area shows the active job and queue.

**Clear queue** removes waiting files. It does not interrupt the file currently being processed.

### Your Music

Each processed song appears in **Your music** and becomes available for playback and alarm selection.

Use the library controls to:

- play a song
- delete a song
- stop playback
- delete all Music

Deleting all Music also removes the available alarm-song inventory. Alarms remain configured and use Random when their selected song no longer exists.

## 9. Podcasts

Podcasts are a separate bedtime/background library.

They do not appear in Music and cannot be selected as alarm songs.

### Podcast volume

Podcast volume is independent from Music and alarm volume.

This allows, for example, loud daytime Music with quiet bedtime Podcasts.

Changes apply while a Podcast is already playing.

### Playing from the web GUI

Open **Your podcasts** and select a Podcast to play it directly.

Starting a Podcast manually counts as played for the no-repeat cycle.

### Ten-tap shortcut

When enabled, ten short taps within eight seconds starts one random Podcast.

The selection is random without replacement:

- only unplayed Podcasts are eligible
- the selected Podcast is marked played when playback starts
- one request plays one MP3
- playback stops when that MP3 ends
- the clock does not automatically chain into another Podcast

When all Podcasts have been played, the library remains at `0 unplayed` until another random Podcast is requested.

On that next request, the clock clears the old play history and randomly selects from the full library again.

This keeps randomness high without repeating Podcasts unnecessarily.

### Reset play history

Use **Reset play history** when you want every Podcast to become eligible again before the current cycle is finished.

Resetting history does not delete any Podcasts.

### Browser Podcast uploads

Use **Add podcasts** for small batches of up to 14 MP3 files.

Browser uploads are staged for processing. Uploading the files does not immediately make them part of the active library.

### Large Podcast libraries by SSH/SFTP

For hundreds of Podcasts, copy files to:

```text
/opt/mk-piclock/assets/podcasts/upload/
```

The upload directory is deliberately not watched in the background. This prevents the clock from trying to transcode files while a large SSH/SFTP transfer is still in progress.

Use this sequence:

1. Copy all MP3 files into the upload directory.
2. Wait for the transfer to finish completely.
3. Open **Podcasts**.
4. Select **Scan uploads**.
5. The GUI shows how many Podcasts were found.
6. Select **Process N podcasts**.
7. Leave the clock to process the batch one file at a time.

### Why Scan uploads is manual

The explicit scan separates file transfer from CPU/audio processing.

This avoids:

- parsing incomplete SSH transfers
- heavy transcoding during file copy
- unnecessary storage contention
- accidental processing just because the Podcasts page was opened

### Podcast processing

Podcast imports use a speech-oriented profile:

```text
MP3
96 kbps
mono
44.1 kHz
16 kHz low-pass
```

Each source follows this sequence:

```text
validate source
process audio
validate output
move output into active library
delete successful source upload
```

A Podcast becomes part of the active library after that individual file processes successfully. The GUI library may update slowly while a large transcode batch is still active because metadata scanning is intentionally secondary to processing.

The entire batch does not have to finish before successful files exist in the library.

### Processing progress

The GUI shows one progress card for the entire batch even if hundreds of files are waiting.

It shows:

- current file
- current file percentage
- batch position, such as `47 of 312`
- overall progress
- processed count
- failed count
- total count

The GUI does not create hundreds of individual processing cards.

### Successful files

A successful file is:

1. processed
2. validated
3. moved to `/opt/mk-piclock/assets/podcasts/`
4. removed from `/opt/mk-piclock/assets/podcasts/upload/`

The source is deleted only after the processed copy has validated and entered the active library.

### Failed files

Failed source files remain in the upload directory.

At the end of the batch, the GUI lists only failures and their reasons.

After correcting the cause, select **Scan uploads** again. Remaining failed files will be found again.

### Unplayed count

**Unplayed X of Y** uses a lightweight library summary and does not wait for every MP3's metadata to be parsed.

If the summary cannot be read, the GUI reports **Unavailable** instead of remaining on Loading indefinitely.

### Library size

The active Podcast scan supports up to 1024 entries.

For a large library, keep enough free SD-card space for both staged uploads and processed output while importing.

## 10. Weather

Weather uses Environment and Climate Change Canada GeoMet data.

The local indoor sensor is independent from Internet weather.

### Inside sensor

The top of the Weather page shows the current AHT10 state, temperature, humidity, and reading age.

If an Inside panel is blank or stale, check this section first.

### Weather source

Choose a common Canadian location or enter an official ECCC GeoMet `citypageweather-realtime` item URL.

Select **Save URL and refresh** after changing the source.

The weather timezone should match how you want local forecast days and specific-time panels interpreted.

### Weather panels

There are three OLED weather slots. Each can be configured independently and may repeat the same source.

Available panel modes are:

**Inside sensor**  
Uses the AHT10 in the clock. No Internet is required.

**Outside now**  
Shows current ECCC conditions.

**Today low / high**  
Shows ECCC's official forecast high and low for the local date. The clock retains the last official value for the date when ECCC later rolls that forecast period out of the live feed.

**Hours ahead**  
Shows a forecast relative to now. Enter 1 through 48 hours.

**Specific time**  
Shows the next occurrence of the selected forecast hour. If today's selected hour has passed, the panel uses tomorrow's matching hour.

Select **Save panels and refresh** after changing panel assignments.

### Weather cache

The clock retains last-good weather.

A reboot or temporary Internet outage therefore does not immediately replace valid cached weather with blank panels.

Fresh data replaces the cache after a successful weather update is accepted by the clock.

### Weather warning chime

The clock can play its built-in message chime when a new active ECCC warning type appears.

Repeated refreshes of the same warning do not repeatedly chime.

You can independently choose whether warnings may chime during bedtime hours.

The warning chime:

- uses global volume
- is capped at 55%
- is skipped while Music or an alarm is already playing

Use **Weather activity** when checking recent weather fetches and warning behavior.

## 11. Display

Display controls determine how the physical OLED looks.

### Clock name

The clock name appears at the top of the web controls.

Use a useful location or purpose, such as `Bedroom Clock`.

### Screen colour

Choose Yellow, Green, or White to match the physical OLED colour.

This setting also keeps browser previews representative of the installed screen.

It does not physically convert one OLED colour into another.

### Bedtime display

Bedtime display automatically dims the OLED between the configured start and end times.

Set:

- whether bedtime dimming is enabled
- night brightness from 0 to 100%
- start time
- end time

Moving the brightness slider previews the real OLED brightness before you save it.

A value of 0% effectively blanks the OLED during bedtime while the clock continues operating.

### Time format

Choose:

- 12-hour, such as `7:30 PM`
- 24-hour, such as `19:30`

### Clock font

Choose the main OLED clock font and its size.

The installed default is DejaVu Sans Mono.

Font size accepts 18 through 54.

Use the preview before saving. A font that looks good on a computer screen may not fit a 256x64 OLED well at a large size.

### INSIDE temperature font

The large indoor temperature may use the clock font or a different installed/uploaded font.

This allows the main clock and indoor panel to be tuned independently.

### Manage fonts

Upload TTF or OTF files under **Manage fonts**.

Uploading a font only adds it to the available list. It does not automatically make it active.

After upload:

1. return to **Clock display**
2. select the uploaded font
3. choose the size
4. save the display settings

Removing an uploaded font removes it from the available custom-font library.

### Screen controls

**Return to clock** restores normal clock display.

**Turn screen off** clears the OLED without shutting down the system.

## 12. System

System contains network, time, diagnostics, storage, security, and backup controls.

### Web password

The web password is optional.

No password means the GUI opens without a password prompt.

If you set one, remember that the clock stores it as plain text locally. Treat it as simple access control for the clock interface, not as high-security credential storage.

Use **Remove password** to return to password-free access.

### Time zone

The clock timezone controls:

- displayed local time
- alarm scheduling
- Weather day boundaries
- Specific-time Weather panels

Changing timezone does not alter UTC or NTP synchronization.

After a timezone change, Weather refreshes using the new local day.

### Hostname

Hostname is the network name advertised by the clock.

Use letters, numbers, hyphens, and periods.

Changing it does not intentionally tear down the current network connection, but future browser connections should use the new hostname.

### NTP source

Normally, leave the NTP server blank and allow system defaults.

Set a custom NTP hostname or IP only when your network requires a particular time source.

A clock without synchronized time can show the correct-looking time temporarily but alarms should not be trusted until Linux confirms NTP synchronization.

### System and Network

Use these sections to confirm:

- software/API versions
- uptime
- current time
- IP address
- hostname
- SSID
- Wi-Fi interface
- signal strength
- NTP synchronization
- system time validity

### Diagnostic report

**Download diagnostic report** creates a support report containing clock and platform diagnostics.

Use it when troubleshooting software, network, storage, sensor, or service problems.

### Platform and Device identity

These sections identify:

- hardware
- OS and kernel
- architecture
- CPU temperature
- inventory ID
- board serial/revision
- machine ID
- CPU signature

They are primarily support information.

### Storage

Storage shows root filesystem information and space used by:

- Music
- Podcasts
- Fonts
- Configuration

Check **Available** before importing a very large Podcast collection.

During bulk processing, both staged source files and processed output can temporarily consume storage.

### SD card

The SD card section reports Linux-visible card identity, capacity, manufacturer, serial, manufacture date, and CID where available.

Use this when confirming which card is installed or investigating storage reliability.

### Inside sensor

The System sensor section shows:

- sensor status
- temperature
- humidity
- last reading
- current error

Use it when the Weather page reports stale or missing indoor data.

### Clock health

Clock health shows whether the core/API and related services are operating correctly.

Use it with **Recent activity** and the diagnostic report when something is not behaving as expected.

### Weather service

This section reports weather-service state separately from the core clock.

A clock can continue showing cached weather even while fresh Internet weather is temporarily unavailable.

## 13. Backup and restore

Use **Download backup** before major configuration changes or when you want a portable copy of the clock setup.

The backup is intended for configuration, not bulk media storage.

Restore replaces:

- settings
- alarms
- fonts
- Weather configuration

Restore leaves these unchanged:

- Music library
- Podcast library
- logs
- caches
- sensor state

This separation avoids placing large Music and Podcast libraries inside routine configuration backups.

## 14. Upgrades

A normal software upgrade is designed to preserve user data.

Persistent clock data includes:

```text
/opt/mk-piclock/config/
/opt/mk-piclock/assets/music/
/opt/mk-piclock/assets/podcasts/
/opt/mk-piclock/assets/podcasts/upload/
/opt/mk-piclock/assets/fonts/
```

Weather configuration and last-good weather state are also preserved.

This means a normal upgrade keeps:

- alarms
- display settings
- audio settings
- Podcast volume and shortcut setting
- Podcast play history
- Music
- processed Podcasts
- staged Podcast uploads
- uploaded fonts
- web settings
- Weather source/panels/cache

The installer also snapshots configuration for transactional rollback if deployment fails.

An upgrade should not be used as a substitute for a configuration backup. Download a backup before unusual/manual changes.

## 15. Recent activity

Recent activity records important clock actions, including alarms, uploads, Weather activity, and control actions.

Use **Refresh** to reload the list.

Use **Clear history** only when the old entries are no longer useful. Clearing the activity log does not reset Podcast play history.

Podcast play history is controlled separately under **Podcasts > Podcast library options**.

## 16. Common tasks

### I want a normal alarm

1. Add Music if required.
2. Open **Alarms**.
3. Turn an alarm On.
4. Set time and days.
5. Choose Random or a specific song.
6. Set start/final volume.
7. Save.
8. Confirm **Next alarm** on Dashboard.

### I want the display dimmer at night

1. Open **Display**.
2. Enable **Bedtime display**.
3. Set start/end times.
4. Move **Night brightness** until the OLED looks right.
5. Save.

### I want to fall asleep to one Podcast

From the clock:

1. Make sure the ten-tap shortcut is enabled.
2. Tap the touch sensor ten times within eight seconds.
3. One random unplayed Podcast starts.
4. The Podcast title replaces the date while the clock remains visible.
5. Tap once to stop early.

From the GUI, open **Podcasts** and play a specific Podcast.

### I want to load hundreds of Podcasts

1. Copy MP3s by SSH/SFTP to `/opt/mk-piclock/assets/podcasts/upload/`.
2. Finish the transfer.
3. Open **Podcasts**.
4. Select **Scan uploads**.
5. Select **Process N podcasts**.
6. Let processing run one file at a time.
7. Review failures after completion.

### I want Podcasts to repeat again

You normally do nothing. After every Podcast has been played, the next random request automatically starts a new no-repeat cycle.

To restart early, use **Reset play history**.

### I want to know whether the room is warming or cooling

Put **Inside sensor** on one of the Weather panels.

The clock displays an up/down arrow only after movement is meaningful. No arrow means the measured trend is currently too small to call rising or falling.

### I want the screen dark but alarms still active

Use **Screen off** or set bedtime brightness to 0%.

Do not shut down the clock.

### My alarm time might be wrong

Check Dashboard for the NTP warning, then open **System** and confirm:

- timezone
- NTP synchronized = yes
- system time valid = yes

### Weather disappeared after Internet loss

The clock should retain last-good weather where available.

Check **Weather activity** and **System > Weather service**. Indoor AHT10 data is independent from Internet weather.

### Podcast import finished with errors

Open Podcasts and review the failed-file list.

Failed sources stay in `/opt/mk-piclock/assets/podcasts/upload/`.

Correct the file/cause, then use **Scan uploads** again.

### A processed Podcast is not immediately visible during a large batch

Processing and library metadata scanning share limited BPI resources. The processed file is promoted individually, but the GUI library may lag while transcoding is active.

Let processing continue. Refresh the library after the batch completes if required.

## 17. Design rules worth knowing

These are intentional behaviors, not limitations to work around.

### Music and Podcasts are separate

Music is the normal/alarm library.

Podcasts are the bedtime/background library.

Keeping them separate prevents hundreds of Podcast episodes from appearing as alarm choices.

### Podcast playback is single-file

A random Podcast request plays one file and stops.

The clock does not automatically play the next episode. This prevents an overnight stream of Podcasts when the purpose is falling asleep to one episode.

### Podcast history is no-repeat, not a playlist order

The clock randomly chooses among remaining unplayed files.

It does not create a fixed shuffled list in advance. Each selection remains random while guaranteeing no repeat within the current cycle.

### Bulk processing is manual

The upload directory is not watched.

You decide when file transfer is finished and when CPU-intensive processing starts.

### Processing is serial

Music and Podcast processing is deliberately conservative on the Banana Pi M2 Zero.

One-at-a-time processing protects GUI response, audio operation, and storage I/O.

### Weather can use cached data

The clock prefers useful last-good weather over immediately blanking the panels when the network disappears.

### Indoor trend is conservative but responsive

The clock does not show an arrow for every sensor fluctuation. It waits for a meaningful smoothed change and uses hysteresis to avoid flicker.

## 18. Support information

Current release:

```text
Product: mk-clock-adult-2.3.54-bpi-m2-zero-r1
HTTP API: v1.62
Private core/API IPC protocol: v35
Base image: bpi-zero-clock 1.0.4-preview36
```

For installation problems, use `INSTALL.md`.

For wiring, use `pinouts.md`.

For older touch wiring, use `HARDWARE_MIGRATION.md`.

For software changes in this release, use `RELEASE_NOTES.md`.
