//go:build unix && !linux

package main

import "os/exec"

func configureParentDeath(cmd *exec.Cmd) {}
