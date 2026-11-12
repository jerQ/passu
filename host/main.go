// Command passu gets passphrases from a passu device (ESP32 diceware
// generator) over USB serial. It is a single static binary with the same
// options and messages as tools/passu.py.
package main

import (
	"bufio"
	"bytes"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"regexp"
	"strings"
	"syscall"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

const usageText = `Get passphrases from a passu device (ESP32 diceware generator) over USB serial.

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
'passu > file' or 'passu | something' only passes the passphrase on.

Options:
  -p, --port PORT    serial device (default: autodetect, or $PASSU_PORT)
  -w, --words N      number of words, 1-32 (default 5)
      --no-digits    leave out the digit after each word (~3.3 bits less per word)
  -s, --spaces       separate the words with spaces instead of '-'
  -n, --count N      number of passphrases (default 1)
  -b, --button       wait for a BOOT button press for each passphrase
                     instead of requesting it over serial
      --copy         copy to clipboard instead of printing (last one if -n > 1)
  -h, --help         show this help
`

const (
	baud = 115200
	// A serial request is answered within milliseconds; give up after this
	// long. Button mode has no limit: it waits for the press until Ctrl+C.
	requestTimeout = 5 * time.Second
	defaultWords   = 5
	maxWords       = 32
)

// USB vendor IDs of common ESP32 board USB-serial chips
var knownVIDs = map[string]string{
	"10C4": "Silicon Labs CP210x",
	"1A86": "WCH CH34x",
	"0403": "FTDI",
	"303A": "Espressif native USB",
}

var (
	// reply to a serial request; a button press prints the bare passphrase
	resultRe = regexp.MustCompile(`PASSPHRASE: (\S+)\s*$`)
	errorRe  = regexp.MustCompile(`PASSPHRASE-ERROR: (.*?)\s*$`)
	// printed at boot after a crash, watchdog reset or brownout
	deviceErrorRe = regexp.MustCompile(`DEVICE-ERROR: (.*?)\s*$`)
	// groups of lowercase word (may contain '-') + one digit, joined by '-'
	formatRe = regexp.MustCompile(`^[a-z][a-z-]*[0-9](?:-[a-z][a-z-]*[0-9])*$`)
)

// fatal is an error that ends the program with its message and exit status 1.
type fatal string

func (f fatal) Error() string { return string(f) }

func fatalf(format string, a ...any) error { return fatal(fmt.Sprintf(format, a...)) }

func info(msg string) { fmt.Fprintln(os.Stderr, msg) }

type options struct {
	port   string
	words  int
	digits bool
	spaces bool
	count  int
	button bool
	copy   bool
}

func parseArgs(args []string) (options, error) {
	var o options
	var noDigits bool
	fs := flag.NewFlagSet("passu", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	for _, name := range []string{"p", "port"} {
		fs.StringVar(&o.port, name, "", "")
	}
	for _, name := range []string{"w", "words"} {
		fs.IntVar(&o.words, name, defaultWords, "")
	}
	fs.BoolVar(&noDigits, "no-digits", false, "")
	for _, name := range []string{"s", "spaces"} {
		fs.BoolVar(&o.spaces, name, false, "")
	}
	for _, name := range []string{"n", "count"} {
		fs.IntVar(&o.count, name, 1, "")
	}
	for _, name := range []string{"b", "button"} {
		fs.BoolVar(&o.button, name, false, "")
	}
	fs.BoolVar(&o.copy, "copy", false, "")
	if err := fs.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return o, err
		}
		return o, fatalf("passu: %v (see passu --help)", err)
	}
	if fs.NArg() > 0 {
		return o, fatalf("passu: unexpected argument %q (see passu --help)", fs.Arg(0))
	}
	o.digits = !noDigits
	if o.count < 1 {
		return o, fatal("passu: --count must be at least 1")
	}
	if o.words < 1 || o.words > maxWords {
		return o, fatalf("passu: --words must be between 1 and %d", maxWords)
	}
	return o, nil
}

func findPort(explicit string) (string, error) {
	if explicit != "" {
		return explicit, nil
	}
	if env := os.Getenv("PASSU_PORT"); env != "" {
		return env, nil
	}
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return "", fatalf("Cannot list serial ports: %v (pass --port)", err)
	}
	var found []string
	var names []string
	for _, p := range ports {
		if !p.IsUSB {
			continue
		}
		if chip, ok := knownVIDs[strings.ToUpper(p.VID)]; ok {
			found = append(found, p.Name)
			names = append(names, fmt.Sprintf("  %s  (%s)", p.Name, chip))
		}
	}
	switch len(found) {
	case 0:
		return "", fatal("No ESP32 found. Is it plugged in with a data cable? " +
			"(or pass --port " + examplePort + ")")
	case 1:
		return found[0], nil
	default:
		return "", fatal("Several possible devices, pick one with --port:\n" +
			strings.Join(names, "\n"))
	}
}

// openPort opens the device with DTR and RTS low: on most ESP32 boards those
// lines drive EN and GPIO0, so leaving them raised would reset the chip.
func openPort(device string) (serial.Port, error) {
	port, err := serial.Open(device, &serial.Mode{
		BaudRate:          baud,
		InitialStatusBits: &serial.ModemOutputBits{DTR: false, RTS: false},
	})
	if err != nil {
		var pe *serial.PortError
		if errors.As(err, &pe) {
			switch pe.Code() {
			case serial.PermissionDenied:
				return nil, fatalf("No permission for %s: %s", device, permissionHint)
			case serial.PortBusy:
				return nil, fatalf("%s is busy - is another serial terminal still open?", device)
			}
		}
		return nil, fatalf("Cannot open %s: %v", device, err)
	}
	if err := port.SetReadTimeout(200 * time.Millisecond); err != nil {
		port.Close()
		return nil, fatalf("Cannot set up %s: %v", device, err)
	}
	return port, nil
}

// lineReader splits the serial stream into lines and stops at a deadline.
type lineReader struct {
	port io.Reader
	buf  []byte
}

// readPassphrase returns the next passphrase from the serial stream, waiting
// at most timeout (0: no limit). With bareOK, an untagged line in the
// passphrase format (a button press) counts too.
func (r *lineReader) readPassphrase(timeout time.Duration, bareOK bool) (string, error) {
	var deadline time.Time
	if timeout > 0 {
		deadline = time.Now().Add(timeout)
	}
	chunk := make([]byte, 256)
	for timeout == 0 || time.Now().Before(deadline) {
		n, err := r.port.Read(chunk)
		if err != nil {
			return "", fatal("Lost connection to the device (unplugged?)")
		}
		r.buf = append(r.buf, chunk[:n]...)
		for {
			i := bytes.IndexByte(r.buf, '\n')
			if i < 0 {
				break
			}
			line := asciiLine(r.buf[:i])
			r.buf = r.buf[i+1:]
			if bareOK && formatRe.MatchString(strings.TrimSpace(line)) {
				return strings.TrimSpace(line), nil
			}
			if m := resultRe.FindStringSubmatch(line); m != nil {
				if formatRe.MatchString(m[1]) {
					return m[1], nil
				}
				info("Ignoring a malformed passphrase line (serial noise?)")
			}
			if m := errorRe.FindStringSubmatch(line); m != nil {
				return "", fatalf("Device reported an error: %s", m[1])
			}
			if m := deviceErrorRe.FindStringSubmatch(line); m != nil {
				return "", fatalf("Device reported: %s. Try again; if it "+
					"keeps happening, don't use the device", m[1])
			}
		}
	}
	return "", fatalf("No answer from the device within %d s (is "+
		"'Generate on Enter in serial console' enabled in the firmware? "+
		"If not, use --button)", int(requestTimeout/time.Second))
}

// asciiLine decodes a line like Python's ascii/errors="replace": anything
// outside 7-bit ASCII becomes U+FFFD, so it can never match the format.
func asciiLine(raw []byte) string {
	var sb strings.Builder
	for _, b := range raw {
		if b < 0x80 {
			sb.WriteByte(b)
		} else {
			sb.WriteRune('�')
		}
	}
	return strings.TrimRight(sb.String(), "\r")
}

// splitGroups splits a validated passphrase into its word+digit groups. A
// group ends at its digit; a '-' inside a word (as in "t-shirt") stays.
func splitGroups(phrase string) []string {
	var groups []string
	start := 0
	for i := 0; i < len(phrase); i++ {
		if phrase[i] >= '0' && phrase[i] <= '9' {
			groups = append(groups, phrase[start:i+1])
			start = i + 2 // skip the '-' after the digit
			i++
		}
	}
	return groups
}

// shape keeps the first words groups (taking a prefix keeps them uniformly
// random), optionally drops each group's digit, and joins them.
func shape(groups []string, words int, digits, spaces bool) string {
	kept := make([]string, 0, words)
	for _, g := range groups[:words] {
		if !digits {
			g = g[:len(g)-1]
		}
		kept = append(kept, g)
	}
	sep := "-"
	if spaces {
		sep = " "
	}
	return strings.Join(kept, sep)
}

func copyToClipboard(text string) error {
	for _, cmd := range clipboardCommands {
		if _, err := exec.LookPath(cmd[0]); err != nil {
			continue
		}
		c := exec.Command(cmd[0], cmd[1:]...)
		c.Stdin = strings.NewReader(text)
		if err := c.Run(); err != nil {
			return fatalf("Clipboard tool %s failed: %v", cmd[0], err)
		}
		return nil
	}
	return fatal("No clipboard tool found (" + clipboardHint + ")")
}

func run(o options) error {
	device, err := findPort(o.port)
	if err != nil {
		return err
	}
	port, err := openPort(device)
	if err != nil {
		return err
	}
	clip, err := session(port, o, os.Stdout)
	// Close the port before starting a clipboard tool: the serial library
	// opens it without close-on-exec, and wl-copy stays running in the
	// background, so it would keep the port open (and busy) after we exit.
	port.Close()
	if err != nil || !o.copy {
		return err
	}
	if err := copyToClipboard(clip); err != nil {
		return err
	}
	info("Copied to clipboard.")
	return nil
}

// session requests o.count passphrases from the device and prints them to
// out. With o.copy nothing is printed and the last passphrase is returned
// for the clipboard instead.
func session(port io.ReadWriter, o options, stdout io.Writer) (string, error) {
	if rb, ok := port.(interface{ ResetInputBuffer() error }); ok {
		time.Sleep(100 * time.Millisecond)
		rb.ResetInputBuffer() // drop anything printed before we came
	}
	r := &lineReader{port: port}
	out := bufio.NewWriter(stdout)
	clip := ""
	for i := 0; i < o.count; i++ {
		var groups []string
		for len(groups) < o.words {
			var timeout time.Duration
			if o.button {
				if len(groups) > 0 {
					info(fmt.Sprintf("Press BOOT again for more words (%d/%d so far)...",
						len(groups), o.words))
				} else {
					label := ""
					if o.count > 1 {
						label = fmt.Sprintf(" (%d/%d)", i+1, o.count)
					}
					info("Press BOOT on the ESP32" + label + "...  (Ctrl+C to cancel)")
				}
			} else {
				if _, err := port.Write([]byte("\n")); err != nil {
					return "", fatal("Lost connection to the device (unplugged?)")
				}
				timeout = requestTimeout
			}
			phrase, err := r.readPassphrase(timeout, o.button)
			if err != nil {
				return "", err
			}
			groups = append(groups, splitGroups(phrase)...)
		}
		phrase := shape(groups, o.words, o.digits, o.spaces)
		if o.copy {
			clip = phrase
		} else {
			fmt.Fprintln(out, phrase)
			out.Flush()
		}
	}
	return clip, nil
}

func main() {
	o, err := parseArgs(os.Args[1:])
	if errors.Is(err, flag.ErrHelp) {
		fmt.Print(usageText)
		return
	}
	if err == nil {
		// Ctrl+C: end the line after the prompt and exit like a shell would.
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
		go func() {
			<-sig
			info("")
			os.Exit(130)
		}()
		err = run(o)
	}
	if err != nil {
		info(err.Error())
		os.Exit(1)
	}
}
