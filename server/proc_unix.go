//go:build unix

package main

import (
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
)

func configureKill(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	configureParentDeath(cmd)
	cmd.Cancel = func() error {
		if filepath.Base(cmd.Path) == "script" {
			if output, err := exec.Command("ps", "-eo", "pid=,ppid=").Output(); err == nil {
				children := map[int][]int{}
				for _, line := range strings.Split(string(output), "\n") {
					fields := strings.Fields(line)
					if len(fields) != 2 {
						continue
					}
					pid, _ := strconv.Atoi(fields[0])
					parent, _ := strconv.Atoi(fields[1])
					children[parent] = append(children[parent], pid)
				}
				var killChildren func(int)
				killChildren = func(parent int) {
					for _, pid := range children[parent] {
						killChildren(pid)
						syscall.Kill(pid, syscall.SIGKILL)
					}
				}
				killChildren(cmd.Process.Pid)
			}
		}
		return syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
	}
}
