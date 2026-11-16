# Serial protocol version 1

Updated host commands require firmware supporting `PASSU/1`. Update the
firmware and host together. There is no automatic fallback to uncorrelated
Enter requests. Manual Enter and button output remain available, including
`--button` with older firmware.

The host generates a fresh 128-bit identifier using its operating system's
random source for every batch, and writes this ASCII line:

```
PASSU/1 <32 lowercase hexadecimal characters>\n
```

The firmware responds with:

```
PASSU/1 <id> <groups> <length> <crc32> <phrase>\n
PASSU/1 <id> ERROR <reason>\n
```

`groups` and `length` are decimal integers. `length` is the ASCII byte length
of the phrase, excluding the newline. `crc32` is eight hexadecimal digits,
CRC-32/ISO-HDLC over the exact phrase bytes (Go `crc32.ChecksumIEEE`, Python
`zlib.crc32`). A successful batch has 5–16 word+digit groups. Hosts verify
the identifier, format, group count, length, and checksum before collecting
any words. Unrelated identifiers and legacy success lines are ignored in
machine mode. Invalid matching responses fail the request. CRC detects
accidental corruption; it does not authenticate the device or its firmware.

The firmware accepts CR, LF, or CRLF line endings, with CRLF counting as
one terminator. Empty lines retain manual generation behavior. Malformed
and overlong nonempty request lines are discarded through the next
terminator, and do not generate a passphrase. Queue-full and generation
errors echo the machine request identifier.

A bare phrase from a button press is retained for manual use. Button mode
checks its format but does not have machine-response integrity metadata.
The host clears already-buffered input at startup; identifiers distinguish
machine replies that arrive later from responses to its own requests.

Run portable protocol tests with `make -C test`, and Go tests with
`cd host && go test ./...`. Protocol tests need no board or pyserial.
