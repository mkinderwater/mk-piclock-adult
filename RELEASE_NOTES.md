# mk-clock-adult 1.2.62 Release Notes

## Stable Weather warning scrolling

Weather warnings now behave as an atomic OLED animation. Once a warning starts, its text and scroll origin remain fixed until that warning reaches a natural boundary.

For a long warning such as:

```text
YELLOW WARNING - AIR QUALITY
```

the marquee completes one full pixel-accurate cycle before the clock considers a refreshed warning list, advances to another warning, repeats the same warning, or restores the date.

## Visual behaviour

- Long warnings begin at the footer's left edge.
- A scrolling warning runs for exactly one complete marquee cycle.
- The next warning is selected only after that cycle completes.
- Weather refreshes cannot replace or restart the active text midway through the scroll.
- The existing 24-pixel gap is retained for seamless repeated scrolling.
- Short warnings remain centred for at least 12 seconds.
- Music metadata still has priority over Weather warnings.

## Compatibility

No API, IPC, Weather configuration, or dashboard format changed.

## Versions

```text
Product:     mk-clock-adult-1.2.62
HTTP API:    1.44
Private IPC: 27
Weather:     Native C 2.0.14
```
