package main

import "testing"

func TestTransferTransitions(t *testing.T) {
	for _, terminal := range []TransferState{TransferDone, TransferFailed, TransferCancelled} {
		for next := range transferTransitions {
			tr := &Transfer{ID: "t", State: terminal}
			if tr.to(next) || tr.State != terminal {
				t.Errorf("%s -> %s was accepted", terminal, next)
			}
		}
	}
	tr := &Transfer{ID: "t", State: TransferStarting}
	if tr.to(TransferUploading) || tr.State != TransferStarting {
		t.Error("starting -> uploading was accepted")
	}
	for _, path := range [][]TransferState{{TransferStarting, TransferDone}, {TransferDone}, {TransferStarting, TransferCancelled}, {TransferFailed}} {
		tr := &Transfer{ID: "t", State: TransferUploading}
		for _, next := range path {
			if !tr.to(next) {
				t.Errorf("uploading path %v rejected at %s", path, next)
			}
		}
	}
}
