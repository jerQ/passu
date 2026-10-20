# Design

This document explains how passu, the ESP32 diceware passphrase
generator, is put together and why. It is written for readers who are
comfortable with C, embedded systems and basic cryptography, and who want
to judge whether the design holds up, not just how to use it.

[SECURITY.md](SECURITY.md) lists each security measure and the known gaps.
This document covers the decisions behind the whole design: what was chosen,
what was rejected, and the trade-offs. The two overlap in places, which is
intentional; each can be read on its own.

## Contents

- [Why a dedicated device](#why-a-dedicated-device)
- [Architecture](#architecture)
- [Passphrase format](#passphrase-format)
- [The randomness pipeline](#the-randomness-pipeline)
- [From bytes to words](#from-bytes-to-words)
- [The wordlist](#the-wordlist)
- [Failing closed](#failing-closed)
- [Memory hygiene](#memory-hygiene)
- [Triggers and concurrency](#triggers-and-concurrency)
- [Output: serial protocol and host command](#output-serial-protocol-and-host-command)
- [Output: the display](#output-the-display)
- [Configuration](#configuration)
- [Testing](#testing)
- [Deliberately not done](#deliberately-not-done)

## Why a dedicated device

A password manager or a diceware program on a laptop is fine for most
people. This project is for when you'd rather not trust a general-purpose
computer to make your most important secrets, such as a password-manager
master password, a disk-encryption passphrase, or a key-backup passphrase.

The device's advantages come from what it *doesn't* have:

- **Little code.** The firmware does one thing, in about 1,600 lines of C
  on top of ESP-IDF, which you can read in an evening.
- **No network, no storage.** The device never joins a network and never
  writes a passphrase to flash. A passphrase exists only in RAM, briefly.
- **Auditable mapping.** The output is built exactly the way you would
  with physical dice and the printed EFF list, so you can check the logic
  against the paper method.

Why an ESP32: it is cheap (a dev board costs about €5) and everywhere, it
has a documented hardware RNG, and ESP-IDF is mature. The board's BOOT
button is all the user interface it needs, so it needs no extra hardware,
and with a display it works without a computer. The cost is trusting Espressif's silicon and closed-source Wi-Fi
binary blobs (see [The randomness pipeline](#the-randomness-pipeline)).

This is a proof of concept. It is tested, but it has not had an independent
review.

## Architecture

The firmware is a pipeline. Each stage sees only the one before it, through
a one-function interface (`rng_t` in `main/rng.h`: a read callback plus its
context, which reads N bytes or fails).

From radio noise to a passphrase, including the failure path:

```
  Wi-Fi radio on, receive only           BOOT button press
  (never connects or scans)              or Enter over serial
            │                                     │
            │ RF noise                            │ CPU cycle count
            ▼                                     │ at the trigger
  ┌────────────────────────────────┐              │
  │ 1. ESP32 hardware RNG          │              │
  │    esp_fill_random()           │              │
  │    radio stopped → read fails  │              │
  └───────────────┬────────────────┘              │
                  │ raw bytes                     │
                  ▼                               │
  ┌────────────────────────────────┐              │
  │ 2. Health tests, every byte    │  failure     │
  │    (NIST SP 800-90B)           ├──────────┐   │
  │    • repetition count: stuck   │          │   │
  │    • adaptive proportion: bias │          │   │
  │    • 1024-byte test at boot    │          │   │
  └───────────────┬────────────────┘          │   │
                  │ 128 raw bytes             │   │
                  ▼                           │   │
  ┌────────────────────────────────┐          │   │
  │ 3. SHA-256 conditioning        │◀─────────┼───┘
  │    SHA-256(128 bytes + cycles) │          │
  │    → 32-byte block; fresh      │          │
  │    input for every block       │          │
  └───────────────┬────────────────┘          │
                  │ random bytes              │
                  ▼                           │
  ┌────────────────────────────────┐          │
  │ 4. Rejection sampling          │          │
  │    d6:    byte < 252 → 1–6     │          │
  │    digit: byte < 250 → 0–9     │          │
  │    other bytes are discarded   │          │
  └───────────────┬────────────────┘          │
                  │ 5 rolls + 1 digit         │
                  ▼                           │
  ┌────────────────────────────────┐          │
  │ 5. EFF large wordlist          │          │
  │    rolls 2-4-3-1-5 → one word  │          │
  │    7776 words; SHA-256 pinned  │          │
  │    at build, checked at boot   │          │
  └───────────────┬────────────────┘          │
                  │ word + digit, × 8 groups  │
                  ▼                           ▼
  ┌────────────────────────────────┐  ┌────────────────────┐
  │ 6. Output (~130 bits)          │  │ PASSPHRASE-ERROR   │
  │    abacus3-zoom7-…             │  │ latched until      │
  │    • serial: passu / monitor   │  │ reset; nothing is  │
  │    • OLED display (optional)   │  │ printed            │
  │    then every buffer is wiped  │  └────────────────────┘
  └────────────────────────────────┘
```

Where each stage lives:

```
.
├── CMakeLists.txt
├── sdkconfig.defaults      project config overrides (sdkconfig is generated)
├── sdkconfig.secure        opt-in lockdown profile (permanent eFuse changes)
├── docs/
│   └── passu.jpg           photo used in the README
├── main/
│   ├── main.c              boot, triggers, output
│   ├── entropy_radio.c/h   radio start, esp_fill_random source      (ESP32)
│   ├── health.c/h          RCT + APT tests, health-checked wrapper  (portable)
│   ├── condition.c/h       SHA-256 conditioning of raw bytes        (portable)
│   ├── dice.c/h            unbiased d6 and digit rolls              (portable)
│   ├── wordlist.c/h        parse/validate list, dice → word         (portable)
│   ├── passphrase.c/h      build word#-word#-..., wipe on error     (portable)
│   ├── display.c/h         OLED framebuffer, text and layout        (portable)
│   ├── font6x8.h           display font (generated by tools/gen_font.py)
│   ├── oled.c/h            SSD1306/SH1106 I2C driver                (ESP32)
│   ├── rng.h               random source interface, secure_zero
│   ├── Kconfig.projbuild   menuconfig options
│   ├── CMakeLists.txt
│   └── eff_large_wordlist.txt   (EFF list, committed)
├── test/
│   ├── test_host.c         host test suite
│   └── Makefile
└── tools/
    ├── passu.py            host command
    └── gen_font.py         generates main/font6x8.h
```

Design rules:

- **Portable core, thin platform layer.** Everything from the health tests
  to the passphrase builder, plus the display layout, is plain C with no
  ESP-IDF code. Only `entropy_radio.c`, `oled.c` and `main.c` touch the
  hardware. This makes the security-relevant logic testable on a PC with
  fake random sources (stuck, biased, scripted, running dry).
- **Each stage can only fail, never degrade.** A stage either delivers what
  it promises or returns `false`. There is no "best effort" path anywhere.
- **Dependency injection, not globals.** The conditioner gets its SHA-256
  function as a pointer: PSA crypto on the device, OpenSSL in the tests.
  The pipeline is assembled once in `app_main`.

## Passphrase format

```
ramrod7-t-shirt8-sworn3-outtakes9-brunt2-finless8-clump5-zoom2
```

Each **group** is one word from the EFF large wordlist plus one decimal
digit. Groups are joined with `-`. The default is 8 groups.

**Why the EFF large list.** 7776 = 6^5 words, so five d6 rolls pick one
word with no waste. The words were chosen to be easy to type and remember
and to avoid offensive or confusable entries, and the list is published, so
anyone can verify it.

**Why a digit per group.** Three reasons:

1. **Entropy.** It adds log2(10) ≈ 3.32 bits per group for one keystroke.
   A group is ≈ 16.25 bits instead of 12.92.
2. **Unambiguous groups.** A few EFF words contain a hyphen (`t-shirt`,
   `yo-yo`). Because every group ends in a digit, the group boundaries are
   always clear: a group ends at a digit followed by `-`. The display
   layout relies on this.
3. **Password rules.** Many systems demand a digit. The passphrase already
   has one, so you never have to add a predictable `1` at the end.

**Why 8 groups by default, and a minimum of 5.** Entropy is counted from
how the generator works, not from the string: 8 × 16.25 ≈ 130 bits. That
is out of reach of any offline guessing attack, even against a fast hash,
with a wide margin. The build refuses fewer than 5 groups (≈ 81 bits): below
that, a fast offline attack becomes thinkable, and nobody should get there
by a typo in a config file.

**Why no capitals or symbols.** They add little entropy per keystroke and
make the passphrase harder to type and remember. Length is the cheapest
source of strength for a human-typed secret.

## The randomness pipeline

### Source: the hardware RNG, with the radio on

ESP-IDF documents that `esp_random()` / `esp_fill_random()` give true random
numbers only while the RF subsystem is running. Otherwise the output is
pseudo-random. The firmware therefore starts Wi-Fi in station mode, never
connects or scans, keeps Wi-Fi settings in RAM only, and turns power saving
off so the radio, and with it the noise, runs all the time.

A Wi-Fi event handler sets an atomic flag when the radio starts and clears
it when it stops. Every read checks the flag before *and after*
`esp_fill_random()`. If the radio stopped during the read, those bytes are
thrown away. The RNG would silently drop to pseudo-random output, and there
is no way to tell from the bytes themselves.

**Trade-off.** This keeps the closed-source Wi-Fi stack running, and it
handles incoming radio frames even when the device isn't connected. The
alternative, `bootloader_random_enable()`, gets noise from internal sources
without the radio, but on the original ESP32 that source is considered
weaker. It is listed as a possible future change, now that conditioning is
in place (see [Deliberately not done](#deliberately-not-done)).

### Health tests: a tripwire, not a proof

The raw bytes go through two continuous tests from NIST SP 800-90B §4.4:

- **Repetition count test (RCT):** the same byte 6 times in a row means the
  source is stuck.
- **Adaptive proportion test (APT):** one value 63 or more times in a
  512-byte window means the source is badly biased.

The cut-offs come from the standard's formulas, with a claimed minimum of
H = 4 bits of entropy per byte and a false-alarm rate of about 2^-20:
RCT = 1 + ⌈20/H⌉ = 6, and APT is the critical binomial value for W = 512,
p = 2^-H. A 1024-byte startup test runs before the device accepts any
request.

**Why H = 4 when the output should be close to 8 bits per byte.** The
hardware whitens its output, so these tests can't measure its real quality.
Claiming less entropy makes the cut-offs looser, which keeps false alarms
practically impossible while still catching a stuck or wildly biased
source instantly. The tests are there to catch hardware failures, not to
certify the RNG.

**Why failures latch.** A source that failed once doesn't become
trustworthy because the next kilobyte looks fine. After a failure, every
read fails until reset.

### Conditioning: SHA-256 over 4× the input

The health-checked bytes are not used directly. Each 32-byte output block
is:

```
block = SHA-256( 128 fresh raw bytes || cycle count at the trigger )
```

**Why condition at all.** The health tests only catch gross failures. A
subtle flaw, such as a slight bias, a correlation between bytes, or a weak
period, would pass straight through to the passphrase. Hashing spreads
whatever entropy the input has across the output. As long as the 128 input
bytes hold at least 256 bits of entropy, the output is close to uniform.

**Why 4:1.** At the 4 bits per byte assumed above, 128 bytes carry
512 bits per 256-bit block. Before calling the output full-entropy,
SP 800-90B asks a vetted conditioner for 64 bits more input entropy than
output (here 320 bits), so there's a good margin. The default 8 groups use
about 50 bytes of output, so 2 blocks, or 256 raw bytes, which takes
microseconds.

**Why not a DRBG (CTR_DRBG or HMAC_DRBG).** A DRBG seeded once and run for
a long time is the usual design, but here it would only add risk:

- It keeps secret state that lives as long as the device runs. That state
  then has to be protected, and a leak of it reveals future output.
- It is more code to review, and the mbedtls DRBG API differs between the
  Mbed TLS versions in ESP-IDF 5 and 6. A one-shot SHA-256 through PSA works
  the same in both.
- A DRBG is for producing large amounts of output from little entropy. This
  device needs about 50 bytes per passphrase and has plenty of raw input.

With direct conditioning, every block depends only on fresh input, and
nothing carries over between blocks or between passphrases.

**Why the cycle count.** The CPU cycle counter is read in the button
interrupt (or when Enter arrives), so it captures the moment of the
request to within a few nanoseconds: a human press, or for `passu` the
host's timing. It is a second input independent of the RNG and
costs nothing. No entropy is *claimed* for it, so even if it were
predictable, nothing is lost.

**Leftovers are discarded.** A passphrase rarely uses an exact multiple of
32 bytes. Unused output is wiped after each passphrase rather than kept for
the next one, so RAM never holds bytes that will become part of a future
passphrase. Used bytes are wiped from the block as they are read.

## From bytes to words

### Rejection sampling, one die at a time

Each d6 roll takes one byte. Bytes 252–255 are discarded and redrawn,
because 252 is the largest multiple of 6 that fits in a byte. Then
`byte % 6 + 1` is the die. Digits work the same way: bytes 250–255 are
discarded, and `byte % 10` is the digit.

**Why not just use `% 6`.** With 256 possible bytes, faces 1–4 would each
come up 43 times and faces 5–6 only 42: a small but real bias, and exactly
the kind of flaw that's hard to spot later.

**Why simulate dice instead of drawing a word index directly.** Drawing a
uniform number below 7776 with rejection sampling would work just as well
and use fewer bytes. Rolling five dice was chosen so the firmware does
exactly what the EFF instructions describe. The dice code (e.g. `24315`)
is an intermediate value you can compare with the printed list, and the
wordlist parser proves that the code-to-word mapping matches it. The
extra bytes cost nothing.

**Why give up after 64 rejections.** With a healthy source, one rejection
has probability ≤ 6/256, so 64 in a row is below 2^-190. Reaching the limit
means the source is broken, and an endless loop would hide that.

## The wordlist

**Embedded unchanged.** `eff_large_wordlist.txt` is embedded in flash
exactly as downloaded, not converted into a C array. Its SHA-256 can
therefore be compared with the published file directly, with nothing in
between that could hide a change.

**Hash pinned, checked twice.** The EFF list's SHA-256 is the Kconfig
default. CMake checks the file at build time, so a corrupted or tampered
download stops the build. The firmware checks again at boot, which catches
flash corruption. Both checks are only as trustworthy as the firmware doing
them: someone who can reflash the device can remove them.

**Strict parsing.** At boot the list is parsed into an index of offsets.
There must be exactly 7776 entries, dice codes in exact order from 11111 to
66666, a separator, a word of 1–16 lowercase letters or hyphens, and nothing
after the last line but whitespace.

**Why so strict.** A list with a dropped, duplicated or swapped line still
"works", but silently changes which words come up, and a list where a few
words appear many times could cut the entropy drastically while every
passphrase still looks fine. Checking the dice codes in order proves that
dice → index → word matches the printed list, without trusting the line
count alone.

## Failing closed

The rule everywhere is: **a weak passphrase is worse than no passphrase.**
You can retry a refusal, but you can't tell a weak passphrase from a strong
one by looking at it.

- A bad wordlist or failed startup test halts the device at boot.
- Health failures and conditioning failures latch until reset.
- A generation error wipes the whole output buffer, so a partial passphrase
  is never printed.
- Errors are tagged lines (`PASSPHRASE-ERROR: ...`) that can never be
  mistaken for a passphrase.
- There is no fallback to a software PRNG, anywhere.

## Memory hygiene

The goal is that once a passphrase has been shown, no copy remains in RAM
that a later bug, debugger or crash dump could reveal.

**`secure_zero`.** Wiping uses a byte loop through a `volatile` pointer.
A plain `memset` before a variable goes out of scope is a dead store, and
compilers remove it. The project once had exactly that bug: `b = 0;` on the
accepted byte in `dice.c`.

**What gets wiped:** the passphrase buffer, dice values, the digit, the word
index, each accepted random byte, the conditioner's input and output, the
trigger record (it holds the cycle count), the display framebuffer and the
I2C transmit buffer. This happens after every passphrase and on every error
path.

**Copies the code can't easily reach:**

- **stdout buffer:** stdout is unbuffered in the generator task, so newlib
  keeps no copy of the printed passphrase.
- **Logging:** passphrases are printed with `printf` only, never through
  `ESP_LOG`. The log level is WARN.
- **Core dumps:** disabled. A core dump is a copy of RAM.
- **Panic output:** a crash reboots silently instead of printing registers,
  which can hold passphrase bytes. After the reboot, the firmware reads the
  reset reason and prints `DEVICE-ERROR: previous run crashed (...)`, so
  the crash isn't hidden.

**What remains** (see SECURITY.md's known gaps): `printf` formats on the
stack, some intermediate values such as word lengths stay in freed stack
memory, and UART driver buffers and your terminal see whatever goes over
serial.

## Triggers and concurrency

Two things can request a passphrase: a button (default GPIO0, the BOOT
button on most boards, so no extra hardware is needed) and Enter on the
serial console. Both just put a small record (`trigger_t`: source and
cycle count) on a FreeRTOS queue.

**One task owns everything sensitive.** A single generator task reads the
queue and is the only code that touches the RNG pipeline, the passphrase
buffer and, after boot, the display. No locks are needed, so there are no
lock bugs, and every buffer has exactly one owner that wipes it.

**Debouncing** happens in that task, by time (300 ms), not in the
interrupt. The interrupt stays trivial: read the cycle counter, queue a
record.

**The serial trigger is a trade-off.** It lets `passu` (and `passu -n 3`)
work without touching the board, but it also lets any process with access
to the serial port request a passphrase. It is a Kconfig option
(`DW_SERIAL_TRIGGER`); with it off, `passu --button` waits for presses.

## Output: serial protocol and host command

### A line-based protocol

Everything the host needs to know is a tagged line:

| Line | Meaning |
|---|---|
| `PASSPHRASE: <phrase>` | Success, requested over serial |
| `<phrase>` (untagged) | Success, requested with the button |
| `PASSPHRASE-DISPLAYED: ...` | Shown on the display only; serial printing is off |
| `PASSPHRASE-ERROR: <reason>` | Generation failed; nothing was produced |
| `DEVICE-ERROR: <reason>` | Crash notice at boot, or no display while serial printing is off |

Tags make the output robust against boot messages, the ROM bootloader's
noise and anything else on the line, and they keep the host side simple.

**Button presses print the bare passphrase.** A button press usually means
a person is reading the serial monitor, and for them the tag is clutter:
one passphrase per line is easier to copy. A serial request comes from the
host command, so it gets the tag. Status and error lines keep their tags
in both cases, so an error can never pass for a passphrase. With
`--button`, the host command also accepts an untagged line, but only if
the whole line matches the passphrase format.

### The host command

`tools/passu.py` is deliberately small and suspicious of its input:

- **No DTR/RTS toggling** when opening the port. On most ESP32 boards these
  lines drive reset and boot mode, and the default pyserial open would
  reset the chip.
- **Old output is dropped.** Anything printed before the command started
  is discarded, so it only returns a passphrase made for this request.
- **Serial request by default.** `passu -n 3` sends Enter three times and
  reads three tagged replies; nobody has to press the button.
  `--button` waits for presses instead.
- **Fixed timeouts, no option.** A serial request is answered within
  milliseconds, so a fixed 5 s limit only catches a hung board or the
  wrong port. A button press waits for a person, so `--button` has no
  limit and Ctrl+C ends it.
- **Format check.** A passphrase is accepted only if it matches the
  word-digit-group format. Line noise is rejected, not passed on.
- **stdout is only the passphrase.** Prompts and errors go to stderr, so
  `passu | something` can't pick up a message by mistake.
- **`--copy`** puts it on the clipboard instead, to keep it out of terminal
  scrollback.
- **`--spaces`** swaps the `-` between groups for a space, for sites or
  habits that prefer it. The separator is fixed, not random, so this
  changes nothing about the passphrase's strength. Only the `-` right after
  a group's digit is replaced, so words that contain a hyphen keep it:
  `yo-yo3-t-shirt8` becomes `yo-yo3 t-shirt8`.

Python can't reliably wipe strings from memory, so the tool only drops its
reference to the passphrase. The host is trusted anyway whenever serial
output is used.

## Output: the display

The optional OLED exists so the passphrase can reach the user without the
computer. With serial printing turned off, it closes the plain-serial gap
described in SECURITY.md.

### One small driver instead of a library

Almost every cheap OLED module uses one of a handful of controllers:
SSD1306 (0.91" and 0.96"), SSD1309 (2.42", accepts SSD1306 commands) and
SH1106 (1.3"). All are 128 px wide, monochrome, and organised in
8-pixel-high pages. The differences are a few init commands and the
SH1106's 132-column RAM, a 2-pixel column offset. One driver with a
controller setting covers them all in under 200 lines.

Rejected alternatives:

- **u8g2** supports hundreds of displays, but it is a large outside
  codebase plus an ESP-IDF adapter, all of which would handle passphrase
  pixels and need review.
- **esp_lcd** has an in-tree SSD1306 driver, but SH1106 needs a separate
  registry component, and its draw API adds little for text-only output.

### Portable rendering, thin transport

`display.c` is pure C: framebuffer, font and layout, all host-tested.
`oled.c` only initialises the controller and copies pages over I2C. The
framebuffer uses the controllers' own layout (one byte = 8 vertical pixels),
so sending it needs no conversion, and one text row is exactly one page.

### Boot diagnostics

Cheap modules and jumper wires make "display not found" the most common
first problem, so the boot log says what happened:

- **Found:** the banner names the controller, size, I2C address and pins.
  The address is shown in both 7-bit form (what menuconfig takes) and
  8-bit form (what many modules print, e.g. `0x78` for `0x3C`).
- **Not found:** the firmware probes every I2C address and logs the ones
  that answer. A device at another address points to the address setting;
  no device at all points to wiring, power, or a module in SPI mode.

Both run once at boot, before any passphrase exists, and log only bus
addresses and configuration.

### Font and layout

- **The font is our own.** Rather than import a bitmap font and track its
  origin and licence, the 5×7 glyphs are drawn as ASCII art in
  `tools/gen_font.py`, which generates `main/font6x8.h`. Anyone can review
  the glyphs by eye.
- **6×8 cells give 21 columns.** The longest group is 10 characters (a
  9-letter word plus digit).
- **The hyphen is 4 px wide instead of 6.** That makes two of the longest
  possible groups, `abcdefghi1-abcdefghi1-`, exactly 128 px, so two groups
  per line always fit.
- **Hyphens are shown.** The screen shows exactly what to type, so
  copying it never means guessing at separators.
- **One group per line when possible.** On 128x64 with 8 groups, each line
  holds one group, the easiest to read off. With more groups than lines,
  two share a line. A compile-time check rejects configurations that can't
  fit (more than 8 groups on 128x32).

### Clearing and wiping

- **RAM:** the framebuffer is drawn, sent, and wiped immediately. The
  controller keeps the image in its own RAM, so the ESP32 doesn't need to.
- **Screen:** the passphrase clears after a timeout (default 60 s), or at
  once on a button press while it is shown. The next press makes a new one.
  That gives a "got it, hide it" gesture without a second button.
- **Failed send:** if an I2C transfer fails partway, the firmware tries to
  draw the idle screen, so half a passphrase isn't left on display.

### Serial printing stays on by default

Turning serial printing off is the more secure setting, since the
passphrase then never leaves the device. It is still on by default so that
adding a display doesn't break the host command, and so the display is
something you choose to rely on deliberately. With it off:

- the device prints `PASSPHRASE-DISPLAYED` instead;
- if the display is missing at boot, the device halts rather than
  generating passphrases nobody can see.

## Configuration

**Kconfig with ranges.** Numeric options have ranges and alternatives are
choice menus, so invalid values fail in menuconfig or the build, not at
runtime. The group count is limited to 5–16. The wordlist hash defaults to
the EFF value.

**`sdkconfig.defaults` is the source of truth; `sdkconfig` isn't tracked.**
ESP-IDF only reads the defaults file when it creates a new `sdkconfig`.
While a full `sdkconfig` was committed, new security defaults, such as the
silent panic reboot, never reached existing checkouts. Now every
project-specific setting lives in `sdkconfig.defaults` or
`main/Kconfig.projbuild`, and `sdkconfig` is a build product.

**Lockdown is a separate profile, not a menuconfig switch.**
`sdkconfig.secure` is layered on top of the defaults in its own build
directory (`build-secure/`). An everyday build can't turn into a
lockdown build by accident, and the lockdown settings are reviewable in one
file. The gate in `main/CMakeLists.txt` refuses any Secure Boot build,
whatever its source, until `CONFIG_DW_LOCKDOWN_ACK` is set.

## Testing

The portable code has a host test suite (`make -C test`), meant to run
with AddressSanitizer and UndefinedBehaviorSanitizer. The firmware itself
has no automated tests; it relies on the shared code being tested and on
manual checks on hardware.

What the tests cover, and why:

- **Fake sources** (stuck, biased, scripted, running dry) check that every
  failure path refuses and wipes. Failure paths are rarely exercised in
  normal use, so they are the most likely to rot.
- **Exact boundaries:** rejection limits, RCT/APT cut-offs, display widths
  down to the last pixel. Off-by-one errors here mean bias or overflows.
- **Known answers:** conditioner output is compared with SHA-256 computed
  independently with OpenSSL, including the extra input.
- **Statistics:** chi-squared tests on d6 rolls and digits, both raw and
  through the conditioner, catch gross bias in the mapping. They can't
  prove uniformity and aren't meant to.
- **Real data:** if the EFF list is present, the parser is run against it
  and the first and last entries are checked (11111 = `abacus`,
  66666 = `zoom`); 2,000 generated passphrases are laid out on both
  display sizes.

In detail, the suite covers:

- **Wordlist validation:** truncated, reordered and malformed lists; CRLF
  line endings; data after the last entry.
- **Rejection sampling:** exact accept/reject boundaries and giving up on a
  broken source.
- **Distribution:** chi-squared tests on d6 rolls and digits.
- **Health tests:** cut-off points, latching, and wiping output after a
  failure.
- **Display:** glyph pixels, text widths and clipping, passphrase layout
  (one and two groups per line, hyphenated words, the widest possible
  groups), refusing what doesn't fit, and wiping the framebuffer.
- **Conditioning:** output matches SHA-256 of the raw bytes and extra data,
  partial reads, wiping used and leftover output, and latching on a source,
  hash or health failure.
- **Passphrases:** format, every word present in the list, and no partial
  output left after an error.

## Deliberately not done

These are open decisions rather than oversights. The reasoning for each:

- **Secure Boot and eFuse lockdown by default.** This is the right answer to
  "someone reflashes the device", and the biggest remaining threat. It's
  available as an opt-in profile (`sdkconfig.secure`), not the default,
  because the eFuse changes are permanent: a mistake bricks the board or
  locks you out of your own firmware, and you have to keep the signing key
  safe. It should be a deliberate step for each device, ideally tried on a
  spare board first.
  - The build requires an explicit acknowledgement and lists every eFuse it
    will burn.
  - Flashing leaves the chip in the ROM loader, so nothing is burned until
    the board is reset, and backing out is still possible up to then.
  - SECURITY.md lists each hardening step with what it costs, and points
    to Espressif's documentation for the full procedure, including
    burning each eFuse by hand.
  - Flash encryption is left out: nothing on the device is secret, and it
    would add another way to brick it.
- **Radio-free noise source.** It would remove the Wi-Fi stack from the
  attack surface, but the internal source is weaker on the original ESP32.
  The SHA-256 conditioning makes it a reasonable option now.
- **Statistical validation of the raw RNG** (SP 800-90B tools, `dieharder`).
  This needs a way to export large amounts of raw output, which is a
  capability you'd rather not have in production firmware. It belongs in a
  separate test build.
- **A crash-loop guard.** A board that keeps crashing should arguably halt
  instead of rebooting forever. It is simple to add, using a counter in
  RTC memory.
- **No storage, no network, no update mechanism.** These are not missing
  features. Each would add code and attack surface, and none of them is
  needed to generate a passphrase.
