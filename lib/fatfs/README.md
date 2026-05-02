# FatFs (vendored)

FatFs - Generic FAT Filesystem Module
Version: R0.15 w/patch1 (released 2022-11-09, copy taken 2026-04-27)
Source: http://elm-chan.org/fsw/ff/

## License

See LICENSE. Permissive single-condition license (functionally BSD-style with
attribution-only requirement). Compatible with any project licensing.

## Local modifications

- `ffconf.h` is authored locally (from upstream template) with our N64-specific
  configuration: 1 volume, 512-byte fixed sector size, FAT16+FAT32 only,
  LFN Mode 1, no RTC, no malloc.
- `diskio.c` is authored locally (the upstream provides `diskio.c` only as
  example documentation) and maps FatFs's required `disk_*` callbacks onto
  Phase 1a's `iodev_sd_*` primitives.
- `ff.c`, `ff.h`, `ffunicode.c`, `diskio.h` are unmodified from upstream.

## Updating

To pull a newer FatFs version:
1. Download the upstream tarball, extract.
2. Diff our `ffconf.h` against the new template; merge their template changes
   into our config.
3. Replace `ff.c`, `ff.h`, `ffunicode.c`, `diskio.h` with the new versions.
4. Rebuild + run `make lib-test` + run BizHawk + manual hardware verification.
5. Update this README's version number.
