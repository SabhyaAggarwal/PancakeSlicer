package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"time"
)

type JobState string

const (
	StateIdle        JobState = "idle"
	StateQueued      JobState = "queued"
	StatePreparing   JobState = "preparing"
	StateSlicing     JobState = "slicing"
	StateRunning              = StateSlicing
	StateCanceling   JobState = "canceling"
	StateDone        JobState = "done"
	StateFailed      JobState = "failed"
	StateCancelled   JobState = "cancelled"
	StateInterrupted JobState = "interrupted"
	StateStale       JobState = "stale"
)

var jobTransitions = map[JobState]map[JobState]bool{
	StateIdle:        {StateQueued: true, StateInterrupted: true},
	StateQueued:      {StatePreparing: true, StateCanceling: true, StateInterrupted: true},
	StatePreparing:   {StateSlicing: true, StateDone: true, StateFailed: true, StateCanceling: true, StateInterrupted: true},
	StateSlicing:     {StateDone: true, StateFailed: true, StateCanceling: true, StateInterrupted: true},
	StateCanceling:   {StateCancelled: true, StateInterrupted: true},
	StateDone:        {StateStale: true},
	StateFailed:      {},
	StateCancelled:   {},
	StateInterrupted: {},
	StateStale:       {},
}

func (s JobState) Finished() bool {
	return s == StateDone || s == StateFailed || s == StateCancelled || s == StateInterrupted || s == StateStale
}

func (s JobState) legacy() JobState {
	switch s {
	case StateIdle:
		return StateQueued
	case StatePreparing, StateSlicing, StateCanceling:
		return "running"
	case StateCancelled:
		return "canceled"
	case StateInterrupted, StateStale:
		return StateFailed
	default:
		return s
	}
}

type JobTransition struct {
	Seq      uint64    `json:"seq"`
	At       time.Time `json:"at"`
	From     JobState  `json:"from"`
	To       JobState  `json:"to"`
	Accepted bool      `json:"accepted"`
	Reason   string    `json:"reason,omitempty"`
}

const maxTransitions = 256

func (j *Job) Transitions() []JobTransition {
	j.mu.Lock()
	defer j.mu.Unlock()
	return append([]JobTransition{}, j.transitions...)
}

func (j *Job) setState(s JobState, reason string) bool {
	j.mu.Lock()
	defer j.mu.Unlock()
	return j.setStateLocked(s, reason)
}

func (j *Job) setStateLocked(s JobState, reason string) bool {
	if s.Finished() && j.stopReason != "" && j.state == StateCanceling {
		s, reason = StateCancelled, j.stopReason
	}
	j.transitionSeq++
	t := JobTransition{Seq: j.transitionSeq, At: time.Now().UTC(), From: j.state, To: s,
		Accepted: jobTransitions[j.state][s], Reason: reason}
	j.transitions = append(j.transitions, t)
	if len(j.transitions) > maxTransitions {
		j.transitions = j.transitions[len(j.transitions)-maxTransitions:]
	}
	if f, err := os.OpenFile(filepath.Join(j.Dir, "transitions.jsonl"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0600); err != nil {
		log.Printf("job %s transition log: %v", j.ID, err)
	} else {
		if err := json.NewEncoder(f).Encode(t); err != nil {
			log.Printf("job %s transition log: %v", j.ID, err)
		}
		f.Close()
	}
	if !t.Accepted {
		log.Printf("job %s illegal transition %s -> %s: %s", j.ID, t.From, t.To, reason)
		return false
	}
	j.state, j.errMsg = s, reason
	if s == StatePreparing {
		j.started = t.At
	}
	if s.Finished() {
		j.finished = t.At
		if j.dropped > 0 {
			j.appendLocked(Event{Type: "log", Stream: "server", Line: fmt.Sprintf("%d further output lines not kept", j.dropped)})
		}
	}
	eventType := "state"
	if s.Finished() {
		eventType = "done"
	}
	j.appendLocked(Event{Type: eventType, State: s.legacy(), LifecycleState: s, Error: reason})
	return true
}
