passu is an ESP32 diceware passphrase generator (proof of concept, no independent security review).

**How to install and use it:** see the [README](https://github.com/jerQ/passu#install-passu-on-the-board).

## Changes

- TODO: fill in before publishing.

## Files

| File | Board |
| --- | --- |
| `passu-{{VERSION}}-esp32.bin` | Classic ESP32 (ESP32-WROOM-32 and similar, with a CP2102/CH340 USB chip) |
| `passu-{{VERSION}}-esp32c3.bin` | ESP32-C3 with native USB (no separate USB chip) |
| `passu-{{VERSION}}-linux-amd64` | The `passu` command for Linux on x86-64 PCs: a single static binary, no Python needed |
| `passu-{{VERSION}}-linux-arm64` | The same for 64-bit ARM Linux (Raspberry Pi OS 64-bit and similar) |
| `SHA256SUMS` | Checksums: `sha256sum -c --ignore-missing SHA256SUMS` |

Flash the `.bin` at address **0x0** at **115200** baud, for example with the [web flasher](https://espressif.github.io/esptool-js/).

To use the `passu` command, rename it to `passu`, make it executable (`chmod +x passu`) and put it on your `PATH`, for example in `~/.local/bin`. Your user needs serial port access (`sudo usermod -aG dialout $USER`, then log in again).

Default settings: 8 word+digit groups (~130 bits), BOOT button (GPIO0 on ESP32, GPIO9 on ESP32-C3) or Enter over serial / the `passu` command. The board needs at least 4 MB of flash.

Built by GitHub Actions from the tagged commit.
