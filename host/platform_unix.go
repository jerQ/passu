//go:build !windows

package main

const examplePort = "/dev/ttyUSB0"

const permissionHint = "add yourself to the 'dialout' group " +
	"(sudo usermod -aG dialout $USER) and log in again"

// Tried in order; the first one found on PATH is used.
var clipboardCommands = [][]string{
	{"wl-copy"},
	{"xclip", "-selection", "clipboard"},
	{"xsel", "--clipboard", "--input"},
	{"pbcopy"},
}

const clipboardHint = "install wl-clipboard or xclip"
