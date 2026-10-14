# ATHENA Icons

The application mark preserves the centered Beuron Condensed `A` and
`#c45aec` background from `ATHENA/misc/pictures/splash/logo.odp`.
SVG files contain outlines, not fonts or embedded bitmaps.

The font is Beuron Condensed by Keno Wehr (2018), licensed under SIL OFL 1.1.
These files contain artwork produced with the font, not a redistributed font.
ATHENA artwork and the generator are distributed under the project's GPL-3.0-or-later license.

Regenerate using fontTools and, for the existing PNG application sizes,
librsvg's `rsvg-convert`:

```sh
python3 tools/icons/generate.py /usr/share/fonts/texlive-beuron/BeuronCondensed-Regular.otf --raster
```

File icons use a purple A directly on white paper: a sheet for `.ath`, a brush for `.ats`,
and stacked sheets for `.avd`. The bundled SVG files serve both desktop MIME
registration and the in-application explorers; no installed font is required.

Desktop installation uses `org.athena.document`, `org.athena.style`, and
`org.athena.avd` icon names. Avoid media-type-prefixed names in MIME declarations:
KDE can resolve their generic fallback in the active theme before reaching the
exact application icon in hicolor.
