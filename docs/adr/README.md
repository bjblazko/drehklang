# Architecture Decision Records

Lightweight MADR-style ADRs. One numbered file per decision, in
chronological order. Once accepted, an ADR is not edited to reflect new
thinking — a later decision that changes course gets its own new ADR that
supersedes the old one (and the old one is marked "Superseded by NNNN").

## Index

| # | Title | Status |
|---|-------|--------|
| [0001](0001-language-and-framework-choice.md) | Language and framework choice | Accepted (platform amended by 0026) |
| [0002](0002-v1-format-and-mcu-scope.md) | v1 format and MCU scope | Accepted (format part superseded by 0016) |
| [0003](0003-testing-strategy.md) | Testing strategy | Accepted |
| [0004](0004-navigation-library-and-index-architecture.md) | Navigation, library indexing, and index cache architecture | Accepted |
| [0005](0005-power-lock-and-round-edge-indicators.md) | Device lock, display power, and round-edge UI widgets | Accepted |
| [0006](0006-audio-task-concurrency.md) | Dedicated FreeRTOS task for audio decode | Accepted (amended by 0026) |
| [0007](0007-battery-indicator.md) | Battery indicator | Accepted (placement superseded by 0008) |
| [0008](0008-braun-design-system-and-screen-redesign.md) | Braun design system and screen redesign | Accepted (Now Playing no-cover layout amended by 0009) |
| [0009](0009-now-playing-spectrum-analyzer.md) | Now Playing spectrum analyzer | Accepted |
| [0010](0010-main-menu-and-settings.md) | Main menu, settings, and display brightness | Accepted |
| [0011](0011-shuffle-and-repeat.md) | Shuffle and repeat | Accepted |
| [0012](0012-resume-session.md) | Resume where you left off | Accepted |
| [0013](0013-jog-shuttle.md) | Jog/shuttle on Now Playing | Accepted (amends 0008's one-edge-ring rule) |
| [0014](0014-now-playing-options-panel.md) | Now Playing options panel | Accepted (amends 0005, 0009, 0011) |
| [0015](0015-sleep-timer.md) | Sleep timer | Accepted (amends 0010's Home layout) |
| [0016](0016-native-formats-and-usb-drive.md) | Native M4A, progressive covers, USB drive mode | Accepted (supersedes 0002's format decision) |
| [0017](0017-two-audio-decode-paths.md) | Two audio decode paths (Vorbis) | Superseded by 0026 |
| [0018](0018-collections-and-menu-visibility.md) | Collections, Home carousel, configurable main menu | Accepted |
| [0019](0019-utf8-tag-text-and-project-text-fonts.md) | UTF-8 tag text and project-generated text fonts | Accepted (supersedes 0008's and 0013's ASCII notes) |
| [0020](0020-patching-esp32-audioi2s-m4a-seek.md) | Patching ESP32-audioI2S at build time to fix M4A seeking | Superseded by 0026 |
| [0021](0021-jump-by-letter-and-music-browse-axes.md) | Jump by letter, and Music browse axes | Accepted (extends 0004, 0018; IndexCache v5) |
| [0022](0022-games-menu-and-table-tennis.md) | A Games menu, and Table Tennis | Accepted |
| [0023](0023-gravity.md) | Gravity, and drawing a game in lines | Accepted (extends 0022) |
| [0024](0024-tone-generator.md) | Tones, a tone generator, and the shared signal layer | Accepted (extends 0018, 0022) |
| [0025](0025-rename-to-drehklang.md) | Renaming the product to Drehklang | Accepted (amends 0012, 0018) |
| [0026](0026-arduino-esp32-3-and-upstream-audioi2s.md) | Arduino-ESP32 3.x, upstream ESP32-audioI2S, and one owner of the DAC | Accepted (supersedes 0017, 0020; amends 0001, 0006, 0013, 0022, 0024) |
| [0027](0027-bluetooth-headphones.md) | Bluetooth headphones through the second chip | Accepted (amends 0026) |
| [0028](0028-now-playing-slot-pages.md) | Now Playing's cover slot as swiped pages | Accepted (amends 0009, 0014, 0024) |
| [0029](0029-equalizer.md) | A seven-band equalizer | Accepted |
| [0030](0030-circuit.md) | Circuit, a pseudo-3D racer with its own renderer | Accepted (extends 0022) |
