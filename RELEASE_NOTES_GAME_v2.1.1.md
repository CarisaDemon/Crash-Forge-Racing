# Crash Forge: Racing v2.1.1 - Experimental

Windows x86 experimental snapshot for community testing.

## Changes
- Show EXPERIMENTAL, the game build number and compilation date/time in the bottom-left of the main menus.
- Arcade AI now chooses installed custom racer bodies and custom karts (retail or custom wheels are allowed). Bots can repeat the only available custom racer; missing/broken mods are skipped.
- Align reserve consumption and jump meter updates with the original 30 Hz gameplay clock at high display FPS.
- Skip fully terrain-occluded distant custom racer render work conservatively.

## Unchanged
- Maximum driving speed and gameplay physics tick rate are not artificially increased at 200 FPS.
- The tested camera extrapolation experiment was discarded due to an approximately 10% performance regression in an eight-bot Oxide Station A/B. The existing camera presentation remains in use.
- This is a game update only; the Crash Forge Launcher is independent.

## Known experimental issues
- Custom track geometry, placement, collisions and some camera angles still need work.
- Heavy GPU/geometry scenes and custom models can lower FPS.
- Please report issues with track, FPS setting, mod loadout, and reproduction steps.

## Install
Provide your own legally obtained original NTSC-U CTR raw MODE2/2352 disc image at assets/ctr-u.bin. No copyrighted disc data, personal mods, saves, profiles or settings are included.
