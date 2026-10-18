package main

import (
	"context"
	"fmt"
	"os/exec"
	"runtime"
	"strings"
)

func lineBufferedCommand(ctx context.Context, tool string, args []string) (*exec.Cmd, error) {
	if runtime.GOOS == "windows" {
		return exec.CommandContext(ctx, tool, args...), nil
	}
	if stdbuf, err := exec.LookPath("stdbuf"); err == nil {
		return exec.CommandContext(ctx, stdbuf, append([]string{"-oL", "-eL", tool}, args...)...), nil
	}
	if script, err := exec.LookPath("script"); err == nil {
		if runtime.GOOS == "linux" {
			quoted := make([]string, 0, len(args)+1)
			for _, arg := range append([]string{tool}, args...) {
				quoted = append(quoted, "'"+strings.ReplaceAll(arg, "'", "'\\''")+"'")
			}
			return exec.CommandContext(ctx, script, "-qefc", strings.Join(quoted, " "), "/dev/null"), nil
		}
		return exec.CommandContext(ctx, script, append([]string{"-q", "/dev/null", tool}, args...)...), nil
	}
	return nil, fmt.Errorf("live tool output requires stdbuf or script on this host")
}
