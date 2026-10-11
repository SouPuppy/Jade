# Hormelz character resources

Author: Hormelz

Source: https://hormelz.itch.io/8-directional-char

Source-page license: Creative Commons Zero v1.0 Universal (CC0-1.0).

Downloaded on 2026-10-11 through the site's free download option. All thirteen
downloadable animation archives and all 104 original PNG sheets are preserved.
The supplied archives contain PNG sheets only; no JSON, Aseprite or timing metadata
was included. Do not infer filenames or row ordering from generic sprite examples.

The game maps screen directions explicitly:

| Game direction | Source suffix |
| --- | --- |
| North | Up |
| NorthEast | UpRight |
| East | Right |
| SouthEast | DownRight |
| South | Down |
| SouthWest | DownLeft |
| West | Left |
| NorthWest | UpLeft |

Walk: 1024×768 sheets, 256×256 cells, four columns, nine populated frames.
ReadyIdle: 1024×1024 sheets, 256×256 cells, four columns, fourteen populated frames.
Frames are read left-to-right and then top-to-bottom. The remaining cells are empty.
Runtime timing defaults to 1/12 second per frame because source durations are absent.
The fixed untrimmed-canvas foot anchor is pixel (128,166), measured from the top-left;
the corresponding bottom-left normalized pivot is (0.5,90/256).

`manifest.json` records SHA-256 hashes for every archive and PNG, and dimensions for
every sheet. Runtime uses the original PNGs directly, without destructive conversion.
