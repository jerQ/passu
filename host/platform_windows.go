//go:build windows

package main

const examplePort = "COM3"

const permissionHint = "another program may be using it"

// clip.exe ships with Windows.
var clipboardCommands = [][]string{{"clip.exe"}}

const clipboardHint = "clip.exe is missing"
