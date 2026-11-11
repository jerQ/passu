#!/usr/bin/env python3
"""
Get passphrases from a passu device (ESP32 diceware generator) over USB serial.

    passu              print one passphrase of 5 words (requested over serial)
    passu -w 8         use 8 words instead of 5 (up to 32)
    passu --no-digits  leave out the digit after each word
    passu --spaces     separate the words with spaces instead of '-'
    passu -n 3         print three passphrases
    passu --button     wait for a BOOT button press for each passphrase
    passu --copy       copy to the clipboard instead of printing

The device sends 8 word+digit groups per request by default. --words keeps
the first N; for more words than one request gives, passu makes more requests
(or, with --button, asks for more presses) and joins the groups. Each group
is independent, so this is as strong as one long passphrase. Each word is
worth ~12.9 bits and each digit ~3.3 bits, so 5 words with
digits are ~81 bits and without digits ~65 bits.

The passphrase goes to stdout; prompts and errors go to stderr, so
`passu > file` or `passu | something` only passes the passphrase on.

Requires pyserial (Fedora: sudo dnf install python3-pyserial).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing: sudo dnf install python3-pyserial "
             "(or pip install --user pyserial)")

BAUD = 115200
# A serial request is answered within milliseconds; give up after this long.
# Button mode has no limit: it waits for the press until Ctrl+C.
REQUEST_TIMEOUT = 5

# USB vendor IDs of common ESP32 board USB-serial chips
KNOWN_VIDS = {
    0x10C4: "Silicon Labs CP210x",
    0x1A86: "WCH CH34x",
    0x0403: "FTDI",
    0x303A: "Espressif native USB",
}

# reply to a serial request; a button press prints the bare passphrase
RESULT = re.compile(r"PASSPHRASE: (\S+)\s*$")
ERROR = re.compile(r"PASSPHRASE-ERROR: (.*?)\s*$")
# printed at boot after a crash, watchdog reset or brownout
DEVICE_ERROR = re.compile(r"DEVICE-ERROR: (.*?)\s*$")
# groups of lowercase word (may contain '-') + one digit, joined by '-'
FORMAT = re.compile(r"[a-z][a-z-]*[0-9](?:-[a-z][a-z-]*[0-9])*")
# the '-' between groups: right after a group's digit, never inside a word
SEPARATOR = re.compile(r"(?<=[0-9])-")
DEFAULT_WORDS = 5
MAX_WORDS = 32


def info(msg):
    print(msg, file=sys.stderr, flush=True)


def find_port(explicit):
    if explicit:
        return explicit
    if os.environ.get("PASSU_PORT"):
        return os.environ["PASSU_PORT"]
    candidates = [p for p in list_ports.comports() if p.vid in KNOWN_VIDS]
    if not candidates:
        sys.exit("No ESP32 found. Is it plugged in with a data cable? "
                 "(or pass --port /dev/ttyUSB0)")
    if len(candidates) > 1:
        names = "\n  ".join(f"{p.device}  ({KNOWN_VIDS[p.vid]})" for p in candidates)
        sys.exit(f"Several possible devices, pick one with --port:\n  {names}")
    return candidates[0].device


def open_port(device):
    """Open without toggling DTR/RTS: on most ESP32 boards those lines drive
    EN and GPIO0, so the default open would reset the chip."""
    port = serial.Serial()
    port.port = device
    port.baudrate = BAUD
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    try:
        port.open()
    except serial.SerialException as e:
        if "Permission denied" in str(e):
            sys.exit(f"No permission for {device}: add yourself to the "
                     "'dialout' group (sudo usermod -aG dialout $USER) "
                     "and log in again")
        if "Device or resource busy" in str(e) or "exclusive" in str(e).lower():
            sys.exit(f"{device} is busy - is idf.py monitor still open?")
        sys.exit(f"Cannot open {device}: {e}")
    return port


def copy_to_clipboard(text):
    for cmd in (["wl-copy"], ["xclip", "-selection", "clipboard"],
                ["xsel", "--clipboard", "--input"]):
        if shutil.which(cmd[0]):
            subprocess.run(cmd, input=text.encode(), check=True)
            return
    sys.exit("No clipboard tool found (install wl-clipboard or xclip)")


def shape(groups, words, digits, spaces):
    """Keep the first `words` groups (taking a prefix keeps them uniformly
    random), optionally drop each group's digit, and join them."""
    groups = groups[:words]
    if not digits:
        groups = [g[:-1] for g in groups]
    return (" " if spaces else "-").join(groups)


def read_passphrase(port, timeout, bare_ok):
    """Return the next passphrase from the serial stream, waiting at most
    `timeout` seconds (None: no limit). With bare_ok, an untagged line in
    the passphrase format (a button press) counts too."""
    deadline = time.monotonic() + timeout if timeout is not None else None
    buf = b""
    while deadline is None or time.monotonic() < deadline:
        buf += port.read(256)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("ascii", errors="replace").rstrip("\r")
            if bare_ok and FORMAT.fullmatch(line.strip()):
                return line.strip()
            m = RESULT.search(line)
            if m:
                phrase = m.group(1)
                if FORMAT.fullmatch(phrase):
                    return phrase
                info("Ignoring a malformed passphrase line (serial noise?)")
            m = ERROR.search(line)
            if m:
                sys.exit(f"Device reported an error: {m.group(1)}")
            m = DEVICE_ERROR.search(line)
            if m:
                sys.exit(f"Device reported: {m.group(1)}. Try again; if it "
                         "keeps happening, don't use the device")
    sys.exit(f"No answer from the device within {REQUEST_TIMEOUT} s (is "
             "'Generate on Enter in serial console' enabled in the firmware? "
             "If not, use --button)")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial device (default: autodetect)")
    ap.add_argument("-n", "--count", type=int, default=1,
                    help="number of passphrases (default 1)")
    ap.add_argument("-b", "--button", action="store_true",
                    help="wait for a BOOT button press for each passphrase "
                         "instead of requesting it over serial")
    ap.add_argument("--copy", action="store_true",
                    help="copy to clipboard instead of printing (last one if -n > 1)")
    ap.add_argument("-w", "--words", type=int, default=DEFAULT_WORDS,
                    help=f"number of words, 1-{MAX_WORDS} (default "
                         f"{DEFAULT_WORDS}); more than the device sends per "
                         "request are collected over several requests")
    ap.add_argument("--no-digits", dest="digits", action="store_false",
                    help="leave out the digit after each word (~3.3 bits "
                         "less per word)")
    ap.add_argument("-s", "--spaces", action="store_true",
                    help="separate the words with spaces instead of '-' "
                         "(same passphrase strength)")
    args = ap.parse_args()
    if args.count < 1:
        ap.error("--count must be at least 1")
    if not 1 <= args.words <= MAX_WORDS:
        ap.error(f"--words must be between 1 and {MAX_WORDS}")

    device = find_port(args.port)
    port = open_port(device)
    try:
        time.sleep(0.1)
        port.reset_input_buffer()          # drop anything printed before we came
        for i in range(args.count):
            groups = []
            while len(groups) < args.words:
                if args.button:
                    label = f" ({i + 1}/{args.count})" if args.count > 1 else ""
                    if groups:
                        info(f"Press BOOT again for more words "
                             f"({len(groups)}/{args.words} so far)...")
                    else:
                        info(f"Press BOOT on the ESP32{label}...  "
                             "(Ctrl+C to cancel)")
                    timeout = None
                else:
                    port.write(b"\n")
                    timeout = REQUEST_TIMEOUT
                groups += SEPARATOR.split(
                    read_passphrase(port, timeout, bare_ok=args.button))
            phrase = shape(groups, args.words, args.digits, args.spaces)
            groups = None
            if args.copy:
                copy_to_clipboard(phrase)
                info("Copied to clipboard.")
            else:
                print(phrase, flush=True)
            phrase = None
    except serial.SerialException:
        sys.exit("Lost connection to the device (unplugged?)")
    except KeyboardInterrupt:
        info("")
        sys.exit(130)
    finally:
        port.close()


if __name__ == "__main__":
    main()
