package main

import (
	"encoding/json"
	"testing"
)

func TestWorkspaceProjectActions(t *testing.T) {
	record := json.RawMessage(`{}`)
	cases := []struct {
		p       workspaceProject
		exists  bool
		state   workspaceProjectState
		allowed []string
	}{
		{workspaceProject{}, false, projectAbsent, []string{"save"}},
		{workspaceProject{Record: record}, true, projectLive, []string{"save", "rename", "duplicate", "archive"}},
		{workspaceProject{Record: record, Archived: true}, true, projectTrashed, []string{"restore"}},
		{workspaceProject{Archived: true}, true, projectTombstone, nil},
	}
	for _, c := range cases {
		if got := workspaceProjectStateOf(c.p, c.exists); got != c.state {
			t.Fatalf("state %s, want %s", got, c.state)
		}
		for _, action := range []string{"save", "rename", "duplicate", "archive", "restore"} {
			want := false
			for _, a := range c.allowed {
				want = want || a == action
			}
			if workspaceProjectActions[c.state][action] != want {
				t.Errorf("%s %s allowed=%v", c.state, action, !want)
			}
		}
	}
}
