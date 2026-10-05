# 0025: Renaming the product to DialHard

## Status

Accepted — 2026-10-05. Amends 0012 (the NVS namespace) and 0018 (the
wordmark and the cache folder).

## Context

The product was called "knobify". That name may clash with an existing
mark, so it changes to **DialHard** before anything is published.

The old name was not only text. It was also the C++ namespace, the
`KNOBIFY_*` build flags, the generated font symbols, the NVS namespace
holding settings and the resume record, and the `/knobify` cache folder on
the SD card. The last two live on devices that are already in use.

## Decision

- **"DialHard" is the name everywhere a person reads it**: Home's
  wordmark, the boot screen, the USB drive's vendor ID, docs and comments.
  The wordmark keeps its capsule, dial and tracking (0018). It is now set
  in camel case, not lowercase. The capsule is sized from the measured
  text, so no layout number changed.
- **`dialhard` / `DIALHARD` is the name in code**: the C++ namespace,
  `dialhard_text_font_*` and `dialhard_icon_font_*`, the `DIALHARD_*`
  build flags, the NVS namespace and the `/dialhard` folder on the SD card.
- **Existing devices are migrated once, at boot.**
  `migrateLegacyNvsNamespace()` copies every u8 and blob from `knobify` to
  `dialhard` before anything reads them, and only then erases the old
  namespace. A power cut part-way through leaves the old namespace intact
  and the next boot copies again. `migrateLegacyCacheDir()` renames
  `/knobify` to `/dialhard` right after the SD card mounts, so the card
  keeps its indexes and covers and needs no rescan.

## Consequences

- Settings, volume and the resume point survive the update.
- If a card has both folders, `/dialhard` wins and `/knobify` is left for
  the user to delete. It is derived state either way (0004).
- The ESP32-audioI2S patch marker is now `DialHard patch:`. A checkout
  whose `.pio/libdeps` was patched under the old marker needs
  `pio pkg install -e esp32-s3` (or the marker edited by hand). Otherwise
  the patch is applied twice.
- The two migration helpers can be deleted once no device runs a
  pre-rename build.
- The GitHub repository is still `bjblazko/knobify`. Renaming it is a
  separate step.
