# Security

This document lists every security measure in passu, the ESP32 diceware
passphrase generator, and its host command `tools/passu.py`, with the
reasoning behind each one. It ends with the known gaps.

This is a proof of concept. It is tested, but it has not had an independent
security review.

## Contents

- [Goals and threat model](#goals-and-threat-model)
- [Randomness](#randomness)
- [Turning random bytes into a passphrase](#turning-random-bytes-into-a-passphrase)
- [Wordlist integrity](#wordlist-integrity)
- [Failing closed](#failing-closed)
- [Keeping the passphrase out of memory and logs](#keeping-the-passphrase-out-of-memory-and-logs)
- [Crashes](#crashes)
- [Network exposure](#network-exposure)
- [Display](#display)
- [Host command](#host-command)
- [Locking down a device](#locking-down-a-device)
- [Build and configuration](#build-and-configuration)
- [Known gaps](#known-gaps)

## Goals and threat model

The device should produce passphrases that are **uniformly random**, have
**enough entropy** to resist offline guessing, and are **not left behind**
anywhere they don't need to be.

What it defends against:

- A weak, broken or subtly biased random number generator.
- Bias introduced by how random bytes are turned into dice rolls.
- A wordlist that is corrupted, truncated, edited or swapped.
- Passphrase copies lingering in RAM, crash dumps or logs.
- Producing a passphrase from bad randomness instead of producing nothing.

What it trusts:

- **The firmware on the board.** Anyone who can reflash it can make it
  output passphrases they know, unless the device is
  [locked down](#locking-down-a-device), which is opt-in
  because it is permanent.
- **The computer it is plugged into.** The passphrase travels over USB
  serial in plain text, unless a display is used with serial printing
  turned off (see [Display](#display)).
- **The ESP32's hardware RNG and ESP-IDF's SHA-256.** The health tests and
  conditioning reduce, but cannot remove, this trust.

## Randomness

### Hardware RNG with the radio running

**What:** passphrases come only from the ESP32's internal hardware RNG
(`esp_fill_random()`). The firmware starts the Wi-Fi radio in station mode,
with power saving off, before generating anything.

**Why:** ESP-IDF documents that the hardware RNG produces true random numbers
only while the RF subsystem is running. Without it, the output is
pseudo-random. Power saving is off so the radio, and with it the noise
source, runs continuously.

### Stop if the radio stops

**What:** a Wi-Fi event handler tracks whether the radio is running. Every
read checks that flag before and after calling `esp_fill_random()`, and fails
if the radio is not running, or stopped during the read.

**Why:** if the radio stopped, the RNG would silently drop to pseudo-random
output. A failed read is safer than a passphrase from a weaker source.

### Continuous health tests

**What:** every raw byte passes two tests modelled on NIST SP 800-90B
section 4.4:

- **Repetition count test:** fails if the same byte appears 6 times in a row.
  This catches a stuck source.
- **Adaptive proportion test:** fails if one value appears 63 or more times
  in a 512-byte window. This catches a heavily biased source.

A 1024-byte startup test runs at boot before the device accepts requests.

**Why:** a broken noise source can look like it works. The cut-offs assume a
minimum of 4 bits of entropy per byte and a false-alarm rate of about 2^-20,
so a healthy source practically never trips them, while a stuck or badly
biased one trips them quickly. The hardware already post-processes its
output, so these tests only catch gross failures. They are a tripwire, not
proof of entropy.

### Health failures latch

**What:** once a test fails, every later read fails until the board is
reset, and the buffer being filled is wiped.

**Why:** a source that failed once can't be trusted again just because the
next few bytes look fine. Wiping the buffer means no partly bad output is
used.

### SHA-256 conditioning

**What:** health-checked bytes are not used directly. Each 32-byte block of
output is the SHA-256 of 128 fresh raw bytes plus the CPU cycle count at the
moment of the button press or Enter (`main/condition.c`). No state carries
over from one block to the next.

**Why:**

- **Hides subtle flaws.** A flaw in the raw output that the health tests
  can't see, such as a slight bias or correlation, is not passed straight
  into the passphrase. SHA-256 spreads whatever entropy the input has evenly
  across the output.
- **Wide margin.** At 4:1, even at the 4 bits per byte the health tests
  assume, each 256-bit block comes from 512 bits of input. That is above the
  256 + 64 bits NIST SP 800-90B asks of a vetted conditioner for
  full-entropy output.
- **An extra, independent input.** The cycle count is taken in the button
  interrupt, or when Enter arrives over serial, so it reflects the exact
  moment of the request (a human press, or the host's timing). It costs
  nothing and doesn't depend on the RNG.
- **No generator state.** Unlike a DRBG, there is no long-lived internal
  state that could leak or be predicted. Every block comes from fresh input.
- **No version-specific crypto API.** It needs only a one-shot SHA-256,
  which works the same in Mbed TLS 3.x (ESP-IDF 5) and 4.x (ESP-IDF 6).

## Turning random bytes into a passphrase

### Rejection sampling

**What:** dice rolls discard bytes that would cause bias. For a d6, bytes
252–255 are discarded and redrawn. For a digit, bytes 250–255 are.

**Why:** `byte % 6` is biased, because 256 is not a multiple of 6: faces 1–4
would come up slightly more often than 5 and 6. Discarding bytes above the
largest multiple of n makes every outcome exactly equally likely.

### Retry limit

**What:** a single roll gives up after 64 discarded bytes in a row.

**Why:** with a healthy source, the chance of 64 rejections in a row is below
2^-190. Reaching the limit means the source is broken, and an endless loop
would hide that.

### Passphrase strength

**What:** each group is one word from the 7776-word EFF list (five d6 rolls)
plus one random digit, about 16.25 bits per group. The default is 8 groups
(about 130 bits). The build only allows 5 to 16 groups, so the minimum is
about 81 bits.

**Why:** 130 bits is far beyond any practical guessing attack, even offline.
The minimum stops a misconfigured build from producing passphrases that are
easy to guess: before, 1 group (about 16 bits) was allowed.

## Wordlist integrity

### Pinned hash, checked at build time and at boot

**What:** `DW_WORDLIST_SHA256` defaults to the SHA-256 of the published EFF
large wordlist (`addd3553…903e`). The build fails if
`main/eff_large_wordlist.txt` doesn't match it. The firmware hashes the
embedded list again at boot and halts on a mismatch.

**Why:** a tampered list could, for example, repeat a few words many times
and cut the real entropy drastically, while the passphrases still look
normal. The build check catches a corrupted or tampered download before it
reaches the board. The boot check catches flash corruption and anything that
changed after the build. Neither protects against someone who can reflash
the board, since they control the code doing the check.

### Strict parsing

**What:** at boot the list is parsed line by line. It must have exactly 7776
entries with dice codes in exact order from 11111 to 66666, each followed by
a tab or space and a word of 1–16 lowercase letters or hyphens. Anything
after the last entry other than whitespace is rejected.

**Why:** this proves that the mapping from dice to word matches the printed
EFF list. A list with a missing, duplicated, reordered or malformed line
would skew the mapping, so the device halts instead.

## Failing closed

**What:** every error stops generation rather than working around it:

- an entropy failure ends generation and wipes the output buffer, so no
  partial passphrase is ever printed;
- health-test and conditioning failures latch until reset;
- a bad wordlist or a failed startup test halts the device at boot;
- errors are printed as tagged `PASSPHRASE-ERROR:` lines, never as
  something that could pass for a passphrase.

**Why:** a passphrase generator that quietly falls back to weaker randomness
is worse than one that refuses. A user can always retry, but can't tell a
weak passphrase apart from a strong one.

## Keeping the passphrase out of memory and logs

### Wiping memory after use

**What:**

- **`secure_zero()`** overwrites memory through a `volatile` pointer, which
  the compiler can't optimise away.
- **What gets wiped:** the passphrase buffer, dice values, the digit, the
  word index, the accepted random byte in each roll, the trigger record, the
  conditioner's input and output, and, with a display, the framebuffer and
  I2C buffer (see [Display](#display)). This happens after every
  passphrase, and on errors too.

**Why:** a plain `memset()` or assignment to a variable that is never read
again is a "dead store", and compilers remove it. Values left in RAM could
leak through a later crash dump, a debugger or a firmware bug. The
accepted-byte wipe in `dice.c` was originally `b = 0`, which was exactly
such a dead store.

### Wiping conditioned output

**What:** each conditioned byte is erased from the buffer as soon as it is
used. Any unused output, and the cycle count, are discarded after each
passphrase.

**Why:** leftover random bytes would otherwise be used for the next
passphrase. Anyone who could read RAM between requests would then know part
of a passphrase that hasn't been generated yet.

### Unbuffered output

**What:** the generator task makes stdout unbuffered.

**Why:** with buffering, `printf` keeps a copy of the passphrase in the C
library's stdout buffer, where `secure_zero()` can't reach it.

### No passphrases in logs

**What:** passphrases are printed with `printf` only, never through
ESP-IDF's logging. The default log level is WARN.

**Why:** log output can be redirected, kept in buffers or sent elsewhere. A
quiet log level also keeps unnecessary detail off the serial port.

### No core dumps

**What:** core dumps are disabled (`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE`).

**Why:** a core dump is a copy of RAM, which can contain a passphrase or the
random bytes behind it, written to flash or serial.

## Crashes

### Silent reboot

**What:** a crash reboots the board without printing the usual panic dump
(`CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT`).

**Why:** the default panic handler prints CPU registers and a backtrace over
serial, and those registers can hold passphrase bytes.

### Crash notice after reboot

**What:** at boot the firmware reads why the chip last reset. After a panic,
watchdog reset or brownout, it prints
`DEVICE-ERROR: previous run crashed (<reason>), device restarted`. The host
command stops and reports it.

**Why:** a silent reboot alone would hide the crash. The user should know a
request failed, and a device that keeps crashing should not be trusted. The
message contains only the reset reason, no memory contents.

## Network exposure

**What:** the radio is started but never connects to or scans for a network.
Wi-Fi settings are kept in RAM only, with flash (NVS) storage disabled.

**Why:** the radio is needed only for its noise. Not joining a network keeps
the device off any network, and nothing persisted means no Wi-Fi
credentials or settings ever live on the device.

## Display

An optional I2C OLED shows the passphrase on the device itself.

### Passphrases can stay off the computer

**What:** with a display, serial printing of passphrases can be turned off
(`DW_SERIAL_PASSPHRASE=n`). The serial port then only reports
`PASSPHRASE-DISPLAYED`. It is on by default so the host command keeps
working.

**Why:** over serial, the computer and every process on it that can open
the port is trusted with the passphrase. Shown only on the display, the
passphrase never leaves the device.

### Framebuffer wiped after every update

**What:** the screen image is drawn in RAM, sent to the display, and wiped
with `secure_zero` straight away. The I2C transmit buffer is wiped too.

**Why:** the display controller keeps the image in its own memory, so the
ESP32 has no reason to keep a copy of the passphrase's pixels.

### Automatic clearing

**What:** the passphrase clears after a timeout (default 60 s), or at once
when the button is pressed while it is shown. Clearing overwrites the
display's memory with the idle screen.

**Why:** a passphrase left on screen can be read by anyone who walks by,
long after it was needed. The press-to-clear lets you hide it the moment
you've copied it.

### No passphrases to nowhere

**What:** if serial printing is off and the display doesn't answer at
boot, the board halts. If a passphrase can't be drawn, it reports
`PASSPHRASE-ERROR: display failure` instead of `PASSPHRASE-DISPLAYED`.

**Why:** generating passphrases nobody can see wastes them at best, and at
worst makes the user think one was shown.

### No outside display library

**What:** the driver (`main/oled.c`), text rendering (`main/display.c`) and
font are written for this project, a few hundred lines in all. The font
comes from ASCII art in `tools/gen_font.py`.

**Why:** everything that handles the passphrase is part of what has to be
trusted and reviewed. A general display library would add far more code
than these few controllers need.

## Host command

`tools/passu.py` reads passphrases from the board over USB serial.

- **Doesn't reset the board.** It opens the port without toggling DTR/RTS,
  which on most ESP32 boards are wired to reset and boot mode. An unintended
  reset in the middle of a request could otherwise drop or confuse output.
- **Ignores old output.** Anything printed before the command started is
  discarded, so a stale passphrase still sitting in the serial buffer, or one
  produced for someone else, is never returned.
- **Requests over serial by default.** It sends Enter for each passphrase
  and accepts only tagged `PASSPHRASE:` replies. With `--button` it waits
  for presses and also accepts the bare line a press prints, but only if
  the whole line is in the passphrase format.
- **Checks the format.** A line is accepted only if it matches the
  word-digit-group format. Serial noise or a corrupted line is rejected
  instead of being passed on as a passphrase.
- **Passphrase only on stdout.** Prompts and errors go to stderr, so
  `passu > file` or `passu | tool` passes on the passphrase and nothing
  else.
- **`--copy`** puts the passphrase on the clipboard instead of printing it,
  which keeps it out of terminal scrollback.
- **`--spaces`** changes only the output: the fixed `-` between groups
  becomes a space after the format check. The separator never carried any
  entropy, so the strength is the same.
- **Exits on device errors.** `PASSPHRASE-ERROR`, `DEVICE-ERROR` and
  `PASSPHRASE-DISPLAYED` lines end the command with an error rather than
  leaving it waiting. A serial request with no answer within 5 seconds
  also ends with an error.
- **Drops its reference** to each passphrase after output. Python can't
  reliably wipe a string from memory, so this only limits how long it stays
  reachable.

## Locking down a device

Out of the box, anyone with USB access can flash different firmware onto
the device. Firmware that prints passphrases the attacker knows would look
exactly like passu. Locking the device down closes that gap: the chip runs
only firmware signed with your key, and the USB flashing path is gone.

> [!WARNING]
> Steps 2–6 below burn eFuses inside the chip. eFuses can only be set, never
> cleared. There is no undo, no factory reset and no recovery over USB. A
> mistake can leave the board unusable. It is opt-in for that reason. Try it
> on a spare board first.

### Steps to secure an ESP32

These are the steps that make a classic ESP32 run only your firmware:

| Step | eFuse | What you lose for good |
| --- | --- | --- |
| 1. Use a chip of revision v3.0 or newer (`esptool -p PORT chip-id`); older chips can't do Secure Boot V2 | None (build setting `ESP32_REV_MIN_3`) | Nothing; the image just won't run on older chips |
| 2. Turn on Secure Boot V2 with your own RSA-3072 signing key | BLOCK2 (key digest), `ABS_DONE_1` | Only firmware signed with this one key will ever run. ESP32 has one key slot and no key revocation. Lose the key and no new firmware can ever run on this board |
| 3. Turn off JTAG | `JTAG_DISABLE` | Hardware debugging |
| 4. Turn off the ROM BASIC console (often already set at the factory) | `CONSOLE_DEBUG_DISABLE` | The ROM fallback console |
| 5. Stop further eFuse read protection | `WR_DIS_EFUSE_RD_DISABLE` | Read-protecting any eFuse later, so flash encryption can't be added afterwards |
| 6. Turn off ROM download mode | `UART_DOWNLOAD_DIS` | `esptool` and `espefuse` can never connect again: no reflashing and no reading or burning eFuses over USB |
| 7. Keep the signing key safe: offline, in at least two places, or [deleted on purpose](#freezing-it-for-good) | None | |

Flash encryption is left out: nothing on the device is secret (see
[No flash encryption](#no-flash-encryption)).

**What this means in practice:** passu has no over-the-air updates, so once
download mode is off, a locked device **runs the firmware you flash now for
the rest of its life**. The only way to change it is to rewrite the flash
chip directly with hardware (an SPI clip) using images signed with your key.
The serial console keeps working, so `passu` and `idf.py monitor` work as
before.

Espressif's documentation explains each of these and the full procedure:

- [Security overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/security.html):
  every ESP32 security feature and how they fit together.
- [Secure Boot V2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v2.html):
  how signing and verification work, key handling, and the eFuses involved.
- [Enable Secure Boot V2 externally](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/security-features-enablement-workflows.html#enable-secure-boot-v2-externally):
  burning each eFuse yourself with `espefuse`, one at a time, before the
  first boot.

### Secure Boot V2

**What:** the lockdown profile (`sdkconfig.secure`) builds a bootloader and
app signed with an RSA-3072 key. The SHA-256 of the public key is burned
into eFuse BLOCK2 and `ABS_DONE_1` turns Secure Boot on. From then on, the
chip's ROM checks the bootloader's signature on every boot, and the
bootloader checks the app's.

**Why:** a device that prints passphrases can't show you which firmware
it's running. Reflashed firmware that prints passphrases the attacker knows
would look and behave exactly like passu. Secure Boot ties the device to
your key. Because the check runs inside the chip, it also holds against
someone who rewrites the external flash chip directly with a clip, which
disabling USB flashing alone wouldn't stop.

**Permanent:** the key digest and `ABS_DONE_1`. ESP32 has a single key
slot and no revocation. A lost key means the board can never run new
firmware.

### No ROM download mode, JTAG or ROM console

**What:** `UART_DOWNLOAD_DIS`, `JTAG_DISABLE` and `CONSOLE_DEBUG_DISABLE`
are burned, and so is `WR_DIS_EFUSE_RD_DISABLE`, which stops any further
eFuse from being read-protected.

**Why:** without download mode, `esptool` can't reach the chip over USB at
all: it can't flash it, read flash or RAM, or burn further eFuses. passu
has no over-the-air update, so a locked device keeps the firmware it was
flashed with. JTAG would allow reading and changing RAM and registers on a
running device. Secure Boot disables it for that reason. Blocking further
read protection is ESP-IDF's Secure Boot default: it keeps the key digest
readable, so the ROM can always check it.

**Permanent:** all four. The board can't be reflashed or recovered over
USB, or debugged, and no eFuse can be read-protected later.

### Every permanent change is acknowledged

**What:**
- The build refuses to run with Secure Boot on until
  `CONFIG_DW_LOCKDOWN_ACK` is set. It lists every eFuse it would burn, and
  repeats the list as a warning on every lockdown build.
- The profile flashes without booting the chip, so nothing is burned
  until the board is reset. Up to then, flashing the normal build backs
  out.
- The bootloader burns all of them together on that first boot.
  Espressif documents how to burn each one with `espefuse` beforehand
  instead. Then every one has to be burned by hand: once `ABS_DONE_1` is
  set, the bootloader treats Secure Boot as set up and burns nothing more.

**Why:** a mistake here can't be fixed. The person flashing should see
each permanent change before it happens, not discover it on first boot.

### No flash encryption

**What:** flash encryption stays off.

**Why:** it protects the *confidentiality* of flash contents, and passu has
nothing secret at rest. The firmware is public, passphrases are never
stored, and there are no Wi-Fi credentials. Integrity, which is what
matters here, comes from Secure Boot. Flash encryption would add another
permanent key and more ways to brick a board. It can't be added to a
locked board later, because its key has to be read-protected.

### Doing it with passu's lockdown profile

`sdkconfig.secure` sets steps 1, 2 and 6 in the build, and ESP-IDF adds
steps 3–5 by default. The bootloader then burns every eFuse in steps 2–6
on the first boot of the signed firmware.

Before you start, make sure the **normal firmware works on this exact
board** (button, display, `passu`). Afterwards, only a new board helps.

1. **Create the signing key** in the project root (`*.pem` is in
   `.gitignore`), then back it up:

   ```bash
   espsecure generate-signing-key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
   ```

2. **Build** in a separate build directory, so your normal build is
   untouched. In menuconfig, enable *passu → I understand that Secure Boot
   eFuse changes are PERMANENT* and set the other options the way you
   tested them. Without that setting the build stops and lists every eFuse
   it would burn:

   ```bash
   idf.py -B build-secure -D SDKCONFIG=build-secure/sdkconfig \
     -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.secure" menuconfig
   idf.py -B build-secure -D SDKCONFIG=build-secure/sdkconfig \
     -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.secure" build
   ```

3. **Flash.** The profile leaves the chip in the ROM loader afterwards
   instead of booting it, so nothing has been burned yet. To back out,
   flash the normal build now, before the board resets:

   ```bash
   idf.py -B build-secure -D SDKCONFIG=build-secure/sdkconfig \
     -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.secure" -p /dev/ttyUSB0 -b 115200 flash
   ```

4. **Boot it: this is the permanent step.** Press EN (reset) or replug,
   then:

   ```bash
   idf.py -B build-secure -p /dev/ttyUSB0 monitor
   ```

   The bootloader burns the eFuses and reports that Secure Boot is
   enabled, then passu prints its usual banner. Generate a passphrase with
   the button and with `passu`. `esptool -p /dev/ttyUSB0 chip-id` should
   now fail to connect; that is expected.

To burn and confirm each eFuse yourself instead, follow Espressif's
*Enable Secure Boot V2 externally* between steps 3 and 4. Then do **all**
of steps 2–6 by hand: once `ABS_DONE_1` is set, the bootloader treats
Secure Boot as already set up and burns nothing else.

The commands use ESP-IDF 6.x tool names (`espsecure`, `espefuse`). Older
versions spell them `espsecure.py`, `espefuse.py`.

### Freezing it for good

After the locked device works, you can delete the signing key. Then
**nobody, you included, can ever produce firmware this board will run**,
even by rewriting the flash chip. That rules out a stolen or leaked key;
the cost is that bugs can never be fixed on this board, only by moving to a
new one. Copies on SSDs, in backups or in synced folders may survive a
plain `rm`, so for a frozen device it's simplest to create the key on a RAM
disk (`/dev/shm`) in the first place and point
`CONFIG_SECURE_BOOT_SIGNING_KEY` at it.

### Remaining limits

- **Fault injection.** Voltage and electromagnetic glitching attacks on
  ESP32 Secure Boot have been published, including against v3 chips. They
  need physical access, lab equipment and time.
- **Device swap.** Someone who replaces your device with a look-alike
  defeats all of this. Physical custody still matters.
- **The signing key** is now the most valuable secret. Keep it offline,
  or delete it on purpose to
  [freeze the device](#freezing-it-for-good).

## Build and configuration

### Security settings live in `sdkconfig.defaults`

**What:** `sdkconfig` is generated and not tracked in git. Every
project-specific setting lives in `sdkconfig.defaults` or
`main/Kconfig.projbuild`.

**Why:** while a full `sdkconfig` was committed, it overrode
`sdkconfig.defaults`, so new security settings such as the silent reboot
never took effect on existing checkouts.

### No build output in git

**What:** `build/` and `build-secure/` are ignored, and so is `*.pem`, so a
Secure Boot signing key in the project root never ends up in a commit.

**Why:** compiled firmware and generated configuration don't belong in the
repository. They make it hard to tell what the reviewed source actually
produces.

### Tests for the security-relevant code

**What:** the portable code (health tests, conditioning, dice, wordlist,
display rendering, passphrase builder) has a host test suite
(`make -C test`), meant to run with AddressSanitizer and
UndefinedBehaviorSanitizer. It covers health-test
cut-offs and latching, conditioning output and wiping, rejection-sampling
boundaries, chi-squared distribution checks, wordlist validation, display
layout and fitting, and wiping output after errors.

**Why:** these properties are easy to break silently. A small bias or a
missing wipe doesn't show up in normal use.

## Known gaps

These are not addressed yet, roughly from most to least important.

- **Firmware can be replaced unless locked down.** By default, Secure
  Boot is off and ROM download mode and JTAG are on, so anyone with USB
  access can flash firmware that outputs passphrases they know, while
  looking normal. The [lockdown](#locking-down-a-device)
  fixes this, but it's opt-in, because its eFuse changes are permanent.
- **Plain serial transport.** Unless a display is used with serial
  printing turned off, the passphrase reaches the computer in plain text.
  Any process that can open the serial port can read it, and, while the
  serial trigger (`DW_SERIAL_TRIGGER`) is on, request one. Copies also
  remain in the USB-serial driver's buffers and in your terminal. Even
  with serial printing off, such a process can still trigger a new
  passphrase, which replaces the one on screen.
- **Wi-Fi stack attack surface.** The closed-source Wi-Fi stack runs the
  whole time and receives radio frames, even though the device never
  connects. A noise source that doesn't need the radio
  (`bootloader_random_enable()`) would remove this, but it is weaker on the
  original ESP32, so it should only be used with the SHA-256 conditioning
  now in place.
- **No transmit check.** The radio shouldn't transmit without a scan or
  connection, but this hasn't been confirmed with a Wi-Fi sniffer.
- **No statistical validation of the raw RNG.** The output hasn't been
  checked with SP 800-90B tools or `dieharder`.
- **Small leftover copies.** `printf` may briefly copy the passphrase onto
  the stack while formatting. Values such as word lengths can remain in
  unused stack memory.
- **No crash-loop guard.** A board that keeps crashing keeps rebooting
  instead of halting.
