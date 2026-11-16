package main

import (
	"errors"
	"fmt"
	"hash/crc32"
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
	return r.readPassphrase(50*time.Millisecond, bareOK, "")
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
	if _, err := r.readPassphrase(0, true, ""); err == nil || !strings.Contains(err.Error(), "Lost connection") {
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

// fakeDevice answers each protocol v1 request with an 8-group response.
type fakeDevice struct {
	pending  []string
	requests int
}

func (d *fakeDevice) Write(p []byte) (int, error) {
	fields := strings.Fields(string(p))
	if len(fields) != 2 || fields[0] != "PASSU/1" || len(fields[1]) != 32 {
		return 0, fmt.Errorf("invalid machine request")
	}
	d.requests++
	phrase := "apple3-t-shirt7-orbit0-canopy5-mellow2-zigzag9-fossil4-tundra1"
	d.pending = append(d.pending, "I (1) noise\r\n"+response(fields[1], phrase))
	return len(p), nil
}

func response(id, phrase string) string {
	return fmt.Sprintf("PASSU/1 %s %d %d %08x %s\n", id, len(splitGroups(phrase)),
		len(phrase), crc32.ChecksumIEEE([]byte(phrase)), phrase)
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

func TestMachineResponse(t *testing.T) {
	id := "00112233445566778899aabbccddeeff"
	phrase := "apple3-orbit0-canopy5-mellow2-zigzag9"
	valid := strings.TrimSuffix(response(id, phrase), "\n")
	got, matched, err := parseResponse(valid, id)
	if !matched || err != nil || got != phrase {
		t.Fatalf("valid: %q %v %v", got, matched, err)
	}
	_, matched, err = parseResponse(valid, strings.Repeat("f", 32))
	if matched || err != nil {
		t.Fatal("unrelated identifier accepted")
	}
	for _, bad := range []string{
		strings.Replace(valid, "apple3", "apple4", 1),
		strings.Replace(valid, " 5 ", " 6 ", 1),
		strings.TrimSuffix(valid, "-zigzag9"),
		"PASSU/1 " + id + " 5 1 00000000 apple3",
		"PASSU/1 " + id + " malformed",
		"PASSU/1 " + id + " ERROR entropy failure",
	} {
		if _, matched, err := parseResponse(bad, id); !matched || err == nil {
			t.Fatalf("accepted invalid matching frame %q", bad)
		}
	}
	got, err = (&lineReader{port: &chunkReader{[]string{
		"PASSPHRASE: apple3\n" + response(strings.Repeat("f", 32), phrase),
		response(id, phrase),
	}}}).readPassphrase(time.Second, false, id)
	if err != nil || got != phrase {
		t.Fatalf("stale reply: %q %v", got, err)
	}
}

type resetFailure struct{ fakeDevice }

func (*resetFailure) ResetInputBuffer() error { return io.ErrUnexpectedEOF }
func TestResetInputFailure(t *testing.T) {
	d := &resetFailure{}
	_, err := session(d, options{words: 5, count: 1}, io.Discard)
	if err == nil || d.requests != 0 {
		t.Fatalf("err %v requests %d", err, d.requests)
	}
}

type shortRequest struct{ fakeDevice }

func (*shortRequest) Write(p []byte) (int, error) { return len(p) - 1, nil }
func TestShortRequest(t *testing.T) {
	_, err := session(&shortRequest{}, options{words: 5, count: 1}, io.Discard)
	if err == nil {
		t.Fatal("short request accepted")
	}
}

func TestFirmwareCRCVector(t *testing.T) {
	frame := "PASSU/1 00112233445566778899aabbccddeeff 5 37 8999a807 apple3-orbit0-canopy5-mellow2-zigzag9"
	if _, matched, err := parseResponse(frame, "00112233445566778899aabbccddeeff"); !matched || err != nil {
		t.Fatalf("firmware CRC vector rejected: %v", err)
	}
}

type failingWriter struct {
	n   int
	err error
}

func (w failingWriter) Write([]byte) (int, error) { return w.n, w.err }

func TestSessionOutputFailure(t *testing.T) {
	for _, tc := range []struct {
		name   string
		writer failingWriter
		want   error
	}{
		{"write failure", failingWriter{0, io.ErrClosedPipe}, io.ErrClosedPipe},
		{"partial write failure", failingWriter{3, io.ErrClosedPipe}, io.ErrClosedPipe},
		{"short write", failingWriter{3, nil}, io.ErrShortWrite},
	} {
		t.Run(tc.name, func(t *testing.T) {
			dev := &fakeDevice{}
			clip, err := session(dev, options{words: 5, digits: true, count: 2}, tc.writer)
			var f fatal
			if !errors.As(err, &f) || err.Error() != "Cannot write passphrase to stdout: "+tc.want.Error() {
				t.Fatalf("got error %v, want stdout failure containing %v", err, tc.want)
			}
			if clip != "" || dev.requests != 1 {
				t.Fatalf("clip %q, requests %d; want empty clip and stop after first request", clip, dev.requests)
			}
		})
	}
}

// Final data must be consumed before reporting the accompanying EOF.
type finalReplies struct{ calls int }

func (r *finalReplies) Read(p []byte) (int, error) {
	r.calls++
	if r.calls > 1 {
		return 0, io.ErrUnexpectedEOF
	}
	return copy(p, "PASSPHRASE: apple3\nPASSPHRASE: orbit0\n"), io.EOF
}
func TestBufferedRepliesBeforeReadError(t *testing.T) {
	port := &finalReplies{}
	r := &lineReader{port: port}
	for _, want := range []string{"apple3", "orbit0"} {
		got, err := r.readPassphrase(time.Second, false, "")
		if err != nil || got != want {
			t.Fatalf("got %q, %v; want %q", got, err, want)
		}
	}
	if port.calls != 1 {
		t.Fatalf("read %d times, want 1", port.calls)
	}
	if _, err := r.readPassphrase(time.Second, false, ""); err == nil {
		t.Fatal("expected connection error after buffered replies")
	}
}

// DEVICE-ERROR lines carry the same advice with and without a request id.
func TestDeviceErrorAdvice(t *testing.T) {
	const want = "Device reported: entropy source failed, halting. Try again; " +
		"if it keeps happening, don't use the device"
	for _, id := range []string{"", "00112233445566778899aabbccddeeff"} {
		r := &lineReader{port: &chunkReader{[]string{"\nDEVICE-ERROR: entropy source failed, halting\n"}}}
		_, err := r.readPassphrase(time.Second, false, id)
		var f fatal
		if !errors.As(err, &f) || err.Error() != want {
			t.Fatalf("id %q: got %v, want %q", id, err, want)
		}
	}
}
