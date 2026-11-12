package main

import (
	"errors"
	"io"
	"reflect"
	"strings"
	"testing"
	"time"
)

func TestSplitGroups(t *testing.T) {
	got := splitGroups("apple3-t-shirt7-yo-yo0-orbit5")
	want := []string{"apple3", "t-shirt7", "yo-yo0", "orbit5"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestShape(t *testing.T) {
	g := []string{"apple3", "t-shirt7", "orbit0"}
	cases := []struct {
		words          int
		digits, spaces bool
		want           string
	}{
		{3, true, false, "apple3-t-shirt7-orbit0"},
		{2, true, true, "apple3 t-shirt7"},
		{2, false, false, "apple-t-shirt"},
		{1, false, true, "apple"},
	}
	for _, c := range cases {
		if got := shape(g, c.words, c.digits, c.spaces); got != c.want {
			t.Errorf("shape(%d,%v,%v) = %q, want %q", c.words, c.digits, c.spaces, got, c.want)
		}
	}
}

// chunkReader returns its chunks one Read at a time, then times out (0, nil)
// like a serial port with a read timeout.
type chunkReader struct{ chunks []string }

func (c *chunkReader) Read(p []byte) (int, error) {
	if len(c.chunks) == 0 {
		time.Sleep(time.Millisecond)
		return 0, nil
	}
	n := copy(p, c.chunks[0])
	c.chunks = c.chunks[1:]
	return n, nil
}

func read(t *testing.T, bareOK bool, chunks ...string) (string, error) {
	t.Helper()
	r := &lineReader{port: &chunkReader{chunks}}
	return r.readPassphrase(50*time.Millisecond, bareOK)
}

func TestReadPassphrase(t *testing.T) {
	// split across reads, CRLF, boot noise before
	got, err := read(t, false, "I (123) boot: junk\r\nPASS", "PHRASE: apple3-orbit0\r", "\n")
	if err != nil || got != "apple3-orbit0" {
		t.Fatalf("got %q, %v", got, err)
	}
	// bare line only counts in button mode
	if got, err := read(t, true, "apple3-orbit0\n"); err != nil || got != "apple3-orbit0" {
		t.Fatalf("button: got %q, %v", got, err)
	}
	if _, err := read(t, false, "apple3-orbit0\n"); err == nil ||
		!strings.Contains(err.Error(), "No answer") {
		t.Fatalf("bare line without button: %v", err)
	}
	// malformed tagged line is skipped, next good one is used
	if got, err := read(t, false, "PASSPHRASE: Apple3-orbit\nPASSPHRASE: apple3\n"); err != nil || got != "apple3" {
		t.Fatalf("malformed: got %q, %v", got, err)
	}
	// non-ASCII byte never passes the format check
	if _, err := read(t, true, "appl\xe9e3\n"); err == nil {
		t.Fatal("non-ASCII line accepted")
	}
	for line, want := range map[string]string{
		"PASSPHRASE-ERROR: entropy health failure\n": "Device reported an error: entropy health failure",
		"DEVICE-ERROR: previous run crashed\n":       "Device reported: previous run crashed.",
	} {
		_, err := read(t, false, line)
		var f fatal
		if !errors.As(err, &f) || !strings.HasPrefix(err.Error(), want) {
			t.Errorf("%q: got %v", line, err)
		}
	}
}

type failReader struct{}

func (failReader) Read([]byte) (int, error) { return 0, io.ErrUnexpectedEOF }

func TestLostConnection(t *testing.T) {
	r := &lineReader{port: failReader{}}
	if _, err := r.readPassphrase(0, true); err == nil || !strings.Contains(err.Error(), "Lost connection") {
		t.Fatalf("got %v", err)
	}
}

func TestParseArgs(t *testing.T) {
	o, err := parseArgs([]string{"--words=8", "--no-digits", "-s", "-n", "2", "--copy", "-p", "/dev/ttyACM0"})
	if err != nil {
		t.Fatal(err)
	}
	want := options{port: "/dev/ttyACM0", words: 8, digits: false, spaces: true, count: 2, copy: true}
	if o != want {
		t.Fatalf("got %+v", o)
	}
	for _, bad := range [][]string{{"-w", "0"}, {"-w", "33"}, {"-n", "0"}, {"extra"}, {"--nope"}} {
		if _, err := parseArgs(bad); err == nil {
			t.Errorf("%q accepted", bad)
		}
	}
}

// fakeDevice answers each "\n" with a tagged 8-group passphrase, like the
// firmware with "Generate on Enter" on.
type fakeDevice struct {
	pending  []string
	requests int
}

func (d *fakeDevice) Write(p []byte) (int, error) {
	for _, b := range p {
		if b == '\n' {
			d.requests++
			d.pending = append(d.pending, "I (1) noise\r\nPASSPHRASE: "+
				"apple3-t-shirt7-orbit0-canopy5-mellow2-zigzag9-fossil4-tundra1\r\n")
		}
	}
	return len(p), nil
}

func (d *fakeDevice) Read(p []byte) (int, error) {
	if len(d.pending) == 0 {
		time.Sleep(time.Millisecond)
		return 0, nil
	}
	n := copy(p, d.pending[0])
	d.pending[0] = d.pending[0][n:]
	if d.pending[0] == "" {
		d.pending = d.pending[1:]
	}
	return n, nil
}

func TestSession(t *testing.T) {
	dev := &fakeDevice{}
	var out strings.Builder
	o := options{words: 10, digits: true, count: 2}
	clip, err := session(dev, o, &out)
	if err != nil || clip != "" {
		t.Fatalf("clip %q, err %v", clip, err)
	}
	line := "apple3-t-shirt7-orbit0-canopy5-mellow2-zigzag9-fossil4-tundra1-apple3-t-shirt7\n"
	if out.String() != line+line {
		t.Fatalf("output %q", out.String())
	}
	if dev.requests != 4 { // 10 words = 2 requests, twice
		t.Fatalf("%d requests, want 4", dev.requests)
	}

	// --copy prints nothing and returns the last passphrase
	out.Reset()
	o = options{words: 3, digits: false, spaces: true, count: 2, copy: true}
	clip, err = session(&fakeDevice{}, o, &out)
	if err != nil || clip != "apple t-shirt orbit" || out.Len() != 0 {
		t.Fatalf("copy: clip %q, out %q, err %v", clip, out.String(), err)
	}
}
