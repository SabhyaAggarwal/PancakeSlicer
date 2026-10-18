//go:build !unix

package main

import "os/exec"

func configureKill(cmd *exec.Cmd) {}
