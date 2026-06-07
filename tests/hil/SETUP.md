# HIL Test Setup — Cold Start

This guide takes you from "Pi in a drawer, never booted" to "first
HIL test green." It is intentionally linear with exactly one
acknowledged branch (Ethernet vs. WiFi at first boot, see §6).

> **Status:** Skeleton landed alongside the bootstrap scripts in
> milestone 1. Polish, screenshots, and per-platform troubleshooting
> tables will land in milestone 5 after the script has been dogfooded
> against several real cold-starts.

## 1. Hardware checklist

- Raspberry Pi 3B
- microSD card, 16 GB or larger (8 GB may run out during nixos-rebuild)
- Pi Camera v1, v2, or v3
- SummerCart64 N64 flashcart
- Nintendo 64 console + AV-to-camera path (camera pointed at TV)
- Ethernet cable to your router (cold start uses Ethernet only)

**Camera ribbon orientation:** on Pi 3B, the **blue stripe faces the
Ethernet jack**. Get this backwards and the camera will not work.

## 2. Build and flash the SD image

```bash
cd pi-sc64
scripts/build-sd-image.sh --ssh-key ~/.ssh/id_ed25519.pub
```

The script prints the image path and a `dd` command. Flash to your
SD card, then insert it into the Pi.

## 3. First boot

Plug the Pi into your router via **Ethernet** (not WiFi — see §6).
Power on. Wait ~30 seconds.

Verify SSH works:

```bash
ssh root@sc64pi.local hostname
```

Expected: prints the hostname. **If this fails**, see §7.

## 4. Bootstrap the Pi

```bash
cd pi-sc64
scripts/bootstrap-pi.sh full sc64pi.local
```

This runs `nixos-rebuild switch` (building on the Pi — slow first
time, ~30 min on a fresh build, much faster afterwards), provisions
a fresh bearer token, and runs a final probe sweep.

The token is saved to `~/.sc64-api-token` on your Mac (mode 600) and
to `/etc/sc64-api/tokens` on the Pi (mode 640, `root:sc64api`).

## 5. Plug in the cart and camera

Now plug the SC64 USB cable into the Pi and connect the Pi Camera
(blue stripe → Ethernet jack).

## 6. WiFi (optional, post-bootstrap)

WiFi is intentionally NOT part of cold start. After the Ethernet
flow above succeeds, you can either:

1. Re-flash with `build-sd-image.sh --ssh-key ... --wifi-ssid <ssid>
   --wifi-psk <psk>` once the script supports it (TODO: not yet
   implemented in milestone 1), or
2. Edit `pi-sc64/hosts/pi/configuration.nix` to add wireless config
   and re-run `bootstrap-pi.sh full`.

The WiFi country-code footgun is real — Ethernet eliminates that
class of failure during initial bring-up.

## 7. First HIL test

When milestone 2 lands, `hil doctor` will verify everything is
green. For now (milestone 1):

```bash
# Verify sc64-api is reachable and authenticated:
curl -s -H "Authorization: Bearer $(cat ~/.sc64-api-token)" \
  http://sc64pi.local:8064/status | head
```

Expected: a JSON status response. (Enriched fields come in milestone 2.)

## 8. Troubleshooting

Once `hil doctor` ships in milestone 2, this section will redirect
you to it: the doctor's first ❌ row and its Fix box are the
authoritative source.

Until then, the most common cold-start failures:

- `ssh: connect to host sc64pi.local: No route to host` — mDNS not
  resolving. Try `dscacheutil -q host -a name sc64pi.local` on
  macOS; if blank, find the Pi's IP via your router's DHCP table.
- `Permission denied (publickey)` — the SD image was flashed without
  your key. Re-flash via `build-sd-image.sh` with the correct
  `--ssh-key` argument.
- `nixos-rebuild` fails on disk space — bigger SD card (16 GB+).
- Camera not detected — check ribbon cable orientation.
