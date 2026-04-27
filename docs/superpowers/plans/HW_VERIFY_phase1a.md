# Phase 1a Hardware Verification

Manual checklist for verifying the SC64 iodev backend on real hardware.
Run AFTER the automated tests pass.

## Setup

Per `CLAUDE.md`'s `Debug printf over SC64 IS-Viewer 64` workflow:

1. Terminal A: `sc64deployer debug --isv 0x03FF0000`
2. Terminal B: `sc64dev` (alias for build + upload — see `~/.zshrc`)
3. Press the physical N64 reset button after upload.

## Test 1: Cart detection

**Expected output in Terminal A:** `[iodev] cart=1 sd_init=...`

- `cart=1` confirms `IODEV_SC64` was detected.
- `sd_init=0` (IODEV_OK) confirms the SD card initialized.
- `sd_init=-1` (IODEV_ERR_NO_CARD) means card slot is empty — insert a card and reboot.
- `sd_init=-3` (IODEV_ERR_IO) means SC64 firmware reported an SD error — check card formatting.

PASS: `cart=1` and `sd_init=0`.

## Test 2: Sector 0 read

This requires a one-off probe routine added to `Practice_Init` temporarily.
After the existing iodev log, append:

```c
{
    static u8 sec0[512] __attribute__((aligned(8)));
    iodev_result_t r = iodev_sd_read_sectors(0, 1, sec0);
    osSyncPrintf("[iodev] read sec0 res=%d  bytes 0..15: ", (int)r);
    for (int i = 0; i < 16; i++) {
        osSyncPrintf("%02X ", sec0[i]);
    }
    osSyncPrintf("\n");
}
```

Build, upload, reset.

**Expected output in Terminal A:** `[iodev] read sec0 res=0  bytes 0..15: <16 hex bytes>`

Compare against host:
```bash
sudo dd if=/dev/diskN bs=512 count=1 status=none | xxd -l 16
```

PASS: `res=0` and the 16-byte prefix matches the host's `dd` output.

## Test 3: Sector write round-trip

Use a dedicated test sector (NOT sector 0 — that's the MBR; corrupting it
makes the SD unbootable until reformatted). Pick a sector well past any
filesystem use, e.g., LBA 0x100000 (~512 MB into the card).

Append to the probe:

```c
{
    static u8 wbuf[512] __attribute__((aligned(8)));
    static u8 rbuf[512] __attribute__((aligned(8)));
    for (int i = 0; i < 512; i++) wbuf[i] = (u8)(i ^ 0x5A);
    iodev_result_t wr = iodev_sd_write_sectors(0x100000, 1, wbuf);
    iodev_result_t rd = iodev_sd_read_sectors(0x100000, 1, rbuf);
    int match = 1;
    for (int i = 0; i < 512; i++) if (wbuf[i] != rbuf[i]) { match = 0; break; }
    osSyncPrintf("[iodev] roundtrip wr=%d rd=%d match=%d\n", (int)wr, (int)rd, match);
}
```

Build, upload, reset.

**Expected output:** `[iodev] roundtrip wr=0 rd=0 match=1`

PASS: all three values are correct.

## Cleanup

After verification, **remove the probe code from `Practice_Init`** before committing further work.

## Reporting

Note in the PR or follow-up commit:
- SC64 firmware version (run `sc64deployer info`).
- SD card size + class (e.g., `SanDisk 64GB Class 10`).
- Any anomalies — first-time SD init delays, intermittent CMD_ERROR, etc.
