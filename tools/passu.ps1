<#
.SYNOPSIS
Get passphrases from a passu device (ESP32 diceware generator) over USB serial.

.DESCRIPTION
Windows counterpart of tools/passu.py. Needs only Windows PowerShell 5.1
(built into Windows 10/11) or PowerShell 7; no Python.

    passu              print one passphrase of 5 words (requested over serial)
    passu -w 8         use 8 words instead of 5 (up to 32)
    passu -NoDigits    leave out the digit after each word
    passu -Spaces      separate the words with spaces instead of '-'
    passu -n 3         print three passphrases
    passu -Button      wait for a BOOT button press for each passphrase
    passu -Copy        copy to the clipboard instead of printing

The device sends 8 word+digit groups per request by default. -Words keeps
the first N; for more words than one request gives, passu makes more
requests (or, with -Button, asks for more presses) and joins the groups.
Each group is independent, so this is as strong as one long passphrase.
Each word is worth ~12.9 bits and each digit ~3.3 bits, so 5 words with
digits are ~81 bits and without digits ~65 bits.

The passphrase goes to stdout; prompts and errors go to stderr, so
`passu > file` or `passu | something` only passes the passphrase on.

.PARAMETER Port
Serial port such as COM3 (default: autodetect, or the PASSU_PORT
environment variable).

.PARAMETER Count
Number of passphrases (default 1).

.PARAMETER Button
Wait for a BOOT button press for each passphrase instead of requesting it
over serial.

.PARAMETER Copy
Copy to the clipboard instead of printing (the last one if -Count > 1).

.PARAMETER Words
Number of words, 1-32 (default 5). More than the device sends per request
are collected over several requests.

.PARAMETER NoDigits
Leave out the digit after each word (~3.3 bits less per word).

.PARAMETER Spaces
Separate the words with spaces instead of '-' (same passphrase strength).
#>
[CmdletBinding()]
param(
    [Alias('p')][string]$Port,
    [Alias('n')][int]$Count = 1,
    [Alias('b')][switch]$Button,
    [switch]$Copy,
    [Alias('w')][int]$Words = 5,
    [switch]$NoDigits,
    [Alias('s')][switch]$Spaces
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

$Baud = 115200
# A serial request is answered within milliseconds; give up after this long.
# Button mode has no limit: it waits for the press until Ctrl+C.
$RequestTimeout = 5

# USB vendor IDs of common ESP32 board USB-serial chips
$KnownVids = @{
    '10C4' = 'Silicon Labs CP210x'
    '1A86' = 'WCH CH34x'
    '0403' = 'FTDI'
    '303A' = 'Espressif native USB'
}

# reply to a serial request; a button press prints the bare passphrase
$ResultRe = 'PASSPHRASE: (\S+)\s*$'
$ErrorRe = 'PASSPHRASE-ERROR: (.*?)\s*$'
# printed at boot after a crash, watchdog reset or brownout
$DeviceErrorRe = 'DEVICE-ERROR: (.*?)\s*$'
# groups of lowercase word (may contain '-') + one digit, joined by '-'
$FormatRe = '^[a-z][a-z-]*[0-9](?:-[a-z][a-z-]*[0-9])*$'
# the '-' between groups: right after a group's digit, never inside a word
$SeparatorRe = '(?<=[0-9])-'
$MaxWords = 32

function Write-Info([string]$Msg) {
    [Console]::Error.WriteLine($Msg)
}

function Exit-WithError([string]$Msg) {
    [Console]::Error.WriteLine($Msg)
    exit 1
}

function Find-Port {
    if ($Port) { return $Port }
    if ($env:PASSU_PORT) { return $env:PASSU_PORT }
    $found = @()
    $devices = Get-CimInstance -ClassName Win32_PnPEntity -Filter "Name LIKE '%(COM%'"
    foreach ($d in $devices) {
        if ($d.Name -notmatch '\((COM\d+)\)') { continue }
        $com = $Matches[1]
        if ($d.PNPDeviceID -match 'VID_([0-9A-F]{4})' -and $KnownVids.ContainsKey($Matches[1])) {
            $found += [pscustomobject]@{ Port = $com; Chip = $KnownVids[$Matches[1]] }
        }
    }
    if ($found.Count -eq 0) {
        Exit-WithError ('No ESP32 found. Is it plugged in with a data cable? ' +
                        '(or pass -Port COM3)')
    }
    if ($found.Count -gt 1) {
        $names = ($found | ForEach-Object { "  $($_.Port)  ($($_.Chip))" }) -join "`n"
        Exit-WithError "Several possible devices, pick one with -Port:`n$names"
    }
    return $found[0].Port
}

function Open-Port([string]$Device) {
    # Keep DTR/RTS low: on most ESP32 boards those lines drive EN and GPIO0,
    # so raising them would reset the chip.
    $sp = New-Object System.IO.Ports.SerialPort $Device, $Baud
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Handshake = [System.IO.Ports.Handshake]::None
    try {
        $sp.Open()
    } catch {
        # .NET errors arrive wrapped in a MethodInvocationException
        $e = $_.Exception
        while ($e.InnerException) { $e = $e.InnerException }
        if ($e -is [System.UnauthorizedAccessException]) {
            Exit-WithError ("$Device is busy - close the web flasher console or any " +
                            'other serial terminal')
        }
        Exit-WithError "Cannot open ${Device}: $($e.Message)"
    }
    return $sp
}

function Read-Passphrase($Sp, $Timeout, [bool]$BareOk) {
    # Return the next passphrase from the serial stream, waiting at most
    # $Timeout seconds ($null: no limit). With $BareOk, an untagged line in
    # the passphrase format (a button press) counts too.
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $buf = ''
    while ($null -eq $Timeout -or $clock.Elapsed.TotalSeconds -lt $Timeout) {
        $n = $Sp.BytesToRead
        if ($n -eq 0) {
            Start-Sleep -Milliseconds 50
            continue
        }
        $bytes = New-Object byte[] $n
        $got = $Sp.Read($bytes, 0, $n)
        # bytes outside 7-bit ASCII become '?', like errors="replace"
        $buf += [System.Text.Encoding]::ASCII.GetString($bytes, 0, $got)
        while ($buf.Contains("`n")) {
            $i = $buf.IndexOf("`n")
            $line = $buf.Substring(0, $i).TrimEnd("`r")
            $buf = $buf.Substring($i + 1)
            # -cmatch: case-sensitive, like the Python version
            if ($BareOk -and $line.Trim() -cmatch $FormatRe) {
                return $line.Trim()
            }
            if ($line -cmatch $ResultRe) {
                $phrase = $Matches[1]
                if ($phrase -cmatch $FormatRe) { return $phrase }
                Write-Info 'Ignoring a malformed passphrase line (serial noise?)'
            }
            if ($line -cmatch $ErrorRe) {
                Exit-WithError "Device reported an error: $($Matches[1])"
            }
            if ($line -cmatch $DeviceErrorRe) {
                Exit-WithError ("Device reported: $($Matches[1]). Try again; if it " +
                                "keeps happening, don't use the device")
            }
        }
    }
    Exit-WithError ("No answer from the device within $RequestTimeout s (is " +
                    "'Generate on Enter in serial console' enabled in the firmware? " +
                    'If not, use -Button)')
}

function Format-Passphrase($Groups) {
    # Keep the first $Words groups (taking a prefix keeps them uniformly
    # random), optionally drop each group's digit, and join them.
    $kept = @($Groups[0..($Words - 1)])
    if ($NoDigits) { $kept = @($kept | ForEach-Object { $_.Substring(0, $_.Length - 1) }) }
    $sep = '-'
    if ($Spaces) { $sep = ' ' }
    return $kept -join $sep
}

if ($Count -lt 1) { Exit-WithError '-Count must be at least 1' }
if ($Words -lt 1 -or $Words -gt $MaxWords) {
    Exit-WithError "-Words must be between 1 and $MaxWords"
}

$device = Find-Port
$sp = Open-Port $device
try {
    Start-Sleep -Milliseconds 100
    $sp.DiscardInBuffer()              # drop anything printed before we came
    for ($i = 0; $i -lt $Count; $i++) {
        $groups = @()
        while ($groups.Count -lt $Words) {
            if ($Button) {
                if ($groups.Count -gt 0) {
                    Write-Info "Press BOOT again for more words ($($groups.Count)/$Words so far)..."
                } else {
                    $label = ''
                    if ($Count -gt 1) { $label = " ($($i + 1)/$Count)" }
                    Write-Info "Press BOOT on the ESP32$label...  (Ctrl+C to cancel)"
                }
                $timeout = $null
            } else {
                $sp.Write([byte[]]@(10), 0, 1)
                $timeout = $RequestTimeout
            }
            $groups += @((Read-Passphrase $sp $timeout $Button.IsPresent) -split $SeparatorRe)
        }
        $phrase = Format-Passphrase $groups
        $groups = $null
        if ($Copy) {
            Set-Clipboard -Value $phrase
            Write-Info 'Copied to clipboard.'
        } else {
            Write-Output $phrase
        }
        $phrase = $null
    }
} catch {
    # .NET errors arrive wrapped in a MethodInvocationException
    $e = $_.Exception
    while ($e.InnerException) { $e = $e.InnerException }
    if ($e -is [System.IO.IOException] -or $e -is [System.InvalidOperationException]) {
        Exit-WithError 'Lost connection to the device (unplugged?)'
    }
    throw
} finally {
    $sp.Close()
}
exit 0
