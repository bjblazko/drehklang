# 0025: Renaming the product to Drehklang

## Status

Accepted — 2026-10-05. Amends 0012 (the NVS namespace) and 0018 (the
wordmark and the cache folder).

## Context

The product was called "knobify". That name may clash with an existing
mark, so it was renamed to "DialHard". That turned out to be a commercial
name too, so the same day it became **Drehklang**. The DialHard build
was flashed to one device and pushed, so it counts as a former name
just like knobify.

The old names were not only text. They were also the C++ namespace, the
build flags, the generated font symbols, the NVS namespace holding
settings and the resume record, and the cache folder on the SD card. The
last two live on devices that are already in use.

## Decision

- **"Drehklang" is the name everywhere a person reads it**: Home's
  wordmark, the boot screen, docs and comments. The wordmark keeps its
  capsule, dial and tracking (0018) and is set as written, capital D. The
  capsule is sized from the measured text, so no layout number changed.
- **As a USB drive the device reports vendor `Huepattl`, product
  `Drehklang`.** The SCSI vendor field holds eight ASCII characters, so
  "Drehklang" does not fit there.
- **`drehklang` / `DREHKLANG` is the name in code**: the C++ namespace,
  `drehklang_text_font_*` and `drehklang_icon_font_*`, the `DREHKLANG_*`
  build flags, the NVS namespace and the `/drehklang` folder on the SD
  card.
- **Existing devices are migrated once, at boot.**
  `migrateLegacyNvsNamespaces()` copies every u8 and blob from `knobify`
  and then `dialhard` into `drehklang` before anything reads them. It
  erases each old namespace only after its copy succeeded. A power cut
  part-way through leaves the old namespace intact and the next boot
  copies again. `migrateLegacyCacheDir()` renames the newest old folder
  (`/dialhard`, else `/knobify`) to `/drehklang` right after the SD card
  mounts, so the card keeps its indexes and covers and needs no rescan.

## Consequences

- Settings, volume and the resume point survive the update, from either
  old name.
- If a card ends up with more than one cache folder, `/drehklang` wins and
  the rest are left for the user to delete. They are derived state either
  way (0004).
- The ESP32-audioI2S patch marker is now `Drehklang patch:`. A checkout
  whose `.pio/libdeps` was patched under an old marker needs
  `pio pkg install -e esp32-s3` (or the marker edited by hand). Otherwise
  the patch is applied twice.
- The two migration helpers can be deleted once no device runs a
  pre-rename build.
- The GitHub repository is `bjblazko/drehklang`. GitHub redirects the old
  names.
