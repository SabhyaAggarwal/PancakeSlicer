package main

import "testing"

func TestEngineHealthTransitions(t *testing.T) {
	h := &engineHealth{state: HealthUnknown, artifactState: HealthUnknown}
	h.setEngine(HealthHealthy)
	h.setArtifact(HealthUnqualified)
	h.setArtifact(HealthQualified)
	h.outputs = map[string]bool{"x": true}
	if h.wireState() != HealthQualified || !h.qualifiedOutput("x") {
		t.Fatalf("qualified engine reads %s", h.wireState())
	}
	h.setEngine(HealthDegraded)
	if h.state != HealthDegraded || h.artifactState != HealthUnqualified || h.qualifiedOutput("x") || h.wireState() != HealthDegraded {
		t.Fatalf("degrade left engine %s artifact %s outputs %v", h.state, h.artifactState, h.outputs)
	}
	h.setEngine(HealthHealthy)
	if h.state != HealthDegraded {
		t.Fatalf("degraded -> healthy was accepted")
	}
	g := &engineHealth{state: HealthUnknown, artifactState: HealthUnknown}
	g.setArtifact(HealthHealthy)
	if g.artifactState != HealthUnknown {
		t.Fatalf("artifact accepted an engine state")
	}
	g.setEngine(HealthHealthy)
	if g.wireState() != HealthHealthy {
		t.Fatalf("no manifest reads %s", g.wireState())
	}
}
