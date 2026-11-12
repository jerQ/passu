# Using passu

Quick reference for a board flashed with the default settings:
8 groups (~130 bits), button on GPIO0 (BOOT), Enter over serial enabled.
Building and flashing: see [BUILDING.md](BUILDING.md#build-and-flash).

## One-time setup (Linux)

From the passu checkout (`~/.local/bin` must be on your `PATH`):

```bash
sudo dnf install python3-pyserial          # or: pip install --user pyserial
sudo usermod -aG dialout $USER             # then log out and back in
chmod +x tools/passu.py
ln -s "$PWD/tools/passu.py" ~/.local/bin/passu
```

## One-time setup (Windows)

Copy `tools/passu.ps1` and `tools/passu.cmd` into one folder on your
`PATH` (the README has a paste-in script). `passu.cmd` runs the script with
`-ExecutionPolicy Bypass`, so no policy change is needed. Windows
PowerShell 5.1, built into Windows 10/11, is enough.

## Get a passphrase

Plug in the board, then:

| Command | Result |
| --- | --- |
| `passu` | One passphrase of 5 word+digit groups (~81 bits) |
| `passu -w 8` (`--words`) | 8 groups instead of 5; 1 to 32. Above 8, `passu` makes several requests (or asks for several BOOT presses) and joins them |
| `passu --no-digits` | Words without the digit (~12.9 instead of ~16.2 bits per word) |
| `passu -s` (`--spaces`) | Words separated by spaces instead of `-` |
| `passu -n 3` | Three passphrases, one per line |
| `passu --copy` | To the clipboard instead of the terminal (with `-n`, only the last one stays) |
| `passu -b` (`--button`) | Wait for a BOOT press instead of requesting over serial |
| `passu -p /dev/ttyACM0` | Use this port (or set `PASSU_PORT`) |

On Windows the options are PowerShell-style: `-w 8` (`-Words`),
`-NoDigits`, `-s` (`-Spaces`), `-n 3` (`-Count`), `-Copy`, `-b`
(`-Button`), `-p COM5` (`-Port`), and `PASSU_PORT` works too. The messages below are the same.

Only the passphrase goes to stdout, so `passu > file` and `passu | cmd`
are safe. Messages and errors go to stderr.

Timeouts are fixed: `passu` gives up if the board doesn't answer within
5 s. `passu --button` waits for each press with no limit; Ctrl+C stops it.

## Without the host command

In `idf.py -p /dev/ttyUSB0 monitor`:

- **BOOT** prints the passphrase on its own line.
- **Enter** prints `PASSPHRASE: <passphrase>`.
- **Ctrl+]**, or **Ctrl+T, then Ctrl+X**, leaves the monitor.

## If something goes wrong

| Message | Meaning / what to do |
| --- | --- |
| `pyserial is missing` | Install it (setup above) |
| `No ESP32 found` | Use a data cable, or give the port with `-p` |
| `Several possible devices` | Pick one with `-p` |
| `... is busy` | Close `idf.py monitor` or the web flasher console (only one program can use the port). On Windows this also covers a port you have no access to |
| `No permission for ...` | Join the `dialout` group (setup above) |
| `Cannot open ...` | The port went away or is wrong; replug, or check `-p` |
| `No answer from the device within 5 s` | Board not running, wrong port, or serial trigger off in the firmware; press EN and retry, or use `--button` |
| `Lost connection to the device` | Cable unplugged or loose; reconnect and retry |
| `No clipboard tool found` | Install `wl-clipboard` (Wayland) or `xclip` for `--copy` (Linux only) |
| `Device reported an error: entropy health failure ...` | RNG fault. Reset the board; if it repeats, don't use it |
| `Device reported an error: ...` (other) | Generation failed; nothing was produced. Retry; if it repeats, don't use it |
| `Device reported: previous run crashed ...` | The board restarted after a crash. Retry; if it repeats, don't use it |

Any error exits with a non-zero status, and no passphrase is printed.
`Ignoring a malformed passphrase line` is only a warning: a garbled line
was skipped. If it keeps appearing, check the cable.

## Good habits

- Keep the passphrase out of terminal scrollback: use `--copy`, or clear
  the terminal afterwards.
- The computer sees every passphrase sent over serial. Don't generate
  passphrases on a computer you don't trust.
