# mk-clock-adult 2.3.50-preview42

Adult Banana Pi M2 Zero clock application paired with `bpi-zero-clock 1.0.4-preview36` and kernel `6.12.101+deb13-armmp`.

Preview36 is a documentation-complete refresh of the compile-corrected playback-only preview35 release. Runtime clock behavior is unchanged.

## Playback-only application

The application is deliberately playback-only. No voice-assistant, microphone, capture, speech DSP, API-key, or related GUI/runtime code is present.

Playback uses the MAX98357A hardware supplied by the base image. The application does not add EQ, resampling, compression or a software mixer to the speaker path. MP3 playback continues to force stereo I2S framing and opens native hardware first.

The rest of the adult clock remains: alarms, music, AHT10 room sensor, OLED display/fonts, weather, bedtime/display policy, system diagnostics/configuration, backups and recent activity.

## Touch wiring revision

Current touch wiring:

```text
TTP223B VCC -> 3.3 V / physical pin 17
TTP223B GND -> GND / physical pin 39
TTP223B OUT -> PA17 / physical pin 37 / gpiochip0 line 17
```

Older Banana Pi adult-clock builds used **PA21 / physical pin 38** for touch. Touch moved to pin 37 during earlier hardware previews. **Touch intentionally remains on pin 37** to avoid requiring another hardware rewiring change. PA21 / physical pin 38 is now free and should remain disconnected.

If upgrading a unit wired to the old pinout, move only the TTP223B `OUT` wire from pin 38 to pin 37. TTP223B power remains 3.3 V on pin 17 and ground on pin 39.

See `HARDWARE_MIGRATION.md` for the old-to-current wiring transition and `pinouts.md` for the complete current header map.

## Compatibility

- Base image: `bpi-zero-clock 1.0.4-preview36`
- Kernel ABI: `6.12.101+deb13-armmp`
- Private core/API IPC protocol: v33
- HTTP API: v1.59
- Hardware profile: `bpi-m2-zero-r1`
- Touch: VCC 3.3 V pin 17; GND pin 39; OUT PA17 pin 37
- PA21 / physical pin 38: free / unassigned
- Audio: MAX98357A playback only

The application hardware verifier reads the generated MAX98357A module and DTB SHA256 values from `/etc/bpi-zero-clock-release`, then verifies those values against the active files. The hashes are not hard-coded into the application because preview36 builds the codec module for the exact 6.12.101 kernel and derives the clock DTB from that kernel's stock Banana Pi DTB.

## Build and install

```bash
make clean
make
make validate-release
./install.sh
```

The installer verifies the paired base image and playback hardware contract before building and deploying the application.

## Documentation

- `pinouts.md` - complete current BPI-M2 Zero wiring and 40-pin header map.
- `HARDWARE_MIGRATION.md` - touch migration from legacy pin 38 / PA21 to current pin 37 / PA17.
- `RELEASE_NOTES.md` - release history and compatibility notes.
