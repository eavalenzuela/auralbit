# Third-party components

auralbit bundles or links against the following third-party software. Each
entry lists where the license text lives and which files are involved.

## Vendored sources (compiled into the auralbit binary)

| Component  | Version / Source | License | License text |
|------------|------------------|---------|--------------|
| miniaudio  | [mackron/miniaudio @ 0.11.21](https://github.com/mackron/miniaudio) | Public Domain *or* MIT-0 (dual) | `third_party/miniaudio/LICENSE`; also embedded in `miniaudio.h` |
| dr_mp3 / dr_flac / dr_wav | [mackron/dr_libs](https://github.com/mackron/dr_libs) | Public Domain *or* MIT-0 (dual) | `third_party/dr_libs/LICENSE`; also embedded in each header |
| stb_vorbis | [nothings/stb](https://github.com/nothings/stb) | MIT *or* Public Domain (dual) | embedded at the bottom of `third_party/stb/stb_vorbis.c` |
| SQLite     | [sqlite.org](https://sqlite.org/) amalgamation 3.50.2 | Public Domain | `third_party/sqlite/LICENSE.md` |
| KissFFT    | [mborgerding/kissfft](https://github.com/mborgerding/kissfft) | BSD-3-Clause | `third_party/kissfft/COPYING` |

## Dynamic system dependencies

| Component  | License | Notes |
|------------|---------|-------|
| Qt 6 (Widgets) | LGPLv3 / GPLv3 / commercial | Linked dynamically against the system-provided `libQt6Widgets`. No Qt sources are redistributed by this repository. |
| TagLib     | LGPLv2.1 / MPL 1.1 (dual) | Linked dynamically against the system-provided `libtag`. No TagLib sources are redistributed by this repository. |
| libmtp (planned) | LGPLv2.1+ | Will be linked dynamically when device-sync ships. |

## Notes

- All vendored single-header / amalgamated sources are committed unmodified;
  the inline license blocks at the top or bottom of each file are intentionally
  preserved so the license travels with the source.
- The KissFFT BSD-3-Clause license requires the copyright notice and disclaimer
  to accompany binary distributions — `third_party/kissfft/COPYING` satisfies
  this when shipped alongside the binary.
- Distributing builds of auralbit must include this file (or an equivalent
  acknowledgement) and the per-component license texts referenced above.
