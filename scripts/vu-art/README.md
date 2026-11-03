# VU meter art

Renders the static images of the full-screen visualization (ADR 0034)
into `lib/visualizer-art/VuMeterArt.{h,c}`. The drawing code is the
approved preview's (`docs/superpowers/specs/2026-11-03-vu-meter-preview.html`);
change both together.

    cd scripts/vu-art
    npm ci
    node generate.mjs --preview /some/dir   # PNGs and two composites to look at

The output is reproducible: a second run writes the same bytes.

## Fonts

From [google/fonts](https://github.com/google/fonts) at commit
`2fe1e8c6b137fb13041a7cfca6fdcd8a910ea4dc`, unmodified, each under the SIL
Open Font License 1.1 (the `OFL-*.txt` next to it):

- `ArchivoNarrow[wght].ttf` (`ofl/archivonarrow`): Copyright 2019 The
  Archivo Narrow Project Authors.
- `ShareTechMono-Regular.ttf` (`ofl/sharetechmono`): Copyright (c) 2012,
  Carrois Type Design, Ralph du Carrois, with Reserved Font Name "Share".

They reach the firmware only as pixels in the generated images.
