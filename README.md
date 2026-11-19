# Timer with counter for Flipper Zero

A countdown for repeated experiments. It starts at `00:05:00`. Set hours, minutes, and seconds for any duration from one second to `23:59:59`. The **Completed** number increases only when a full countdown finishes. Both the count and selected duration are saved on the microSD card and restored when the app opens again.

## Controls

| Button | Action |
| --- | --- |
| OK | Start, pause, resume, or start the next run after completion |
| Left / Right | Move the underline between hours, minutes, and seconds while ready or after a completed run |
| Up / Down | Increase or decrease the underlined field; hold to repeat |
| Right while running or paused | Restart the selected countdown without changing the count |
| Hold OK | Clear the completed count while stopped or paused |
| Back | Exit the app; an unfinished countdown is discarded |

When a run finishes, the Flipper gives four short high beeps and vibrates, similar to [Count Down Timer by 0w0mewo](https://github.com/0w0mewo/fpz_cntdown_timer). The next run starts when you press OK, so time between experiments is not counted. A save error appears on screen if the SD card cannot store the count.

## Build

This folder is a uFBT external-app project. With Python and uFBT installed, run `ufbt` from this folder. The resulting `experiment_timer.fap` is in `dist/`; copy it to `apps/Tools/` on the Flipper's microSD card. You can also use `ufbt launch` while the Flipper is connected over USB. The internal app ID and FAP filename remain `experiment_timer` so the saved counter stays available after the display name changes.

The count and selected duration live in the app's data folder as `count.bin`. Counts and durations saved by the earlier minute-only version remain intact after this update.
