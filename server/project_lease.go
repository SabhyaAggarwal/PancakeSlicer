package main

import (
	"encoding/json"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"time"
)

const (
	leaseTTL       = 45 * time.Second
	leaseHeartbeat = 15 * time.Second
	sessionHeader  = "X-Orca-Session"
)

type projectLease struct {
	Session  string `json:"session"`
	Label    string `json:"label"`
	Since    int64  `json:"since"`
	LastSeen int64  `json:"lastSeen"`
	Previous string `json:"previous,omitempty"`
}
type leaseHolder struct {
	Label    string `json:"label"`
	Since    int64  `json:"since"`
	LastSeen int64  `json:"lastSeen"`
}

func (l *projectLease) live(now time.Time) bool {
	return l != nil && now.UnixMilli()-l.LastSeen < leaseTTL.Milliseconds()
}
func (l *projectLease) holder() leaseHolder { return leaseHolder{l.Label, l.Since, l.LastSeen} }

func validSession(s string) bool { return len(s) >= 8 && len(s) <= 100 && workspaceID(s) }

func (st *workspaceStore) leaseTable() map[string]*projectLease {
	if st.leases != nil {
		return st.leases
	}
	st.leases = map[string]*projectLease{}
	raw, err := os.ReadFile(filepath.Join(st.root, "leases.json"))
	if err == nil && json.Unmarshal(raw, &st.leases) == nil {
		now := time.Now().UnixMilli()
		for _, l := range st.leases {
			l.LastSeen = now
		}
	} else {
		st.leases = map[string]*projectLease{}
	}
	return st.leases
}

func (st *workspaceStore) saveLeases() {
	now := time.Now()
	for id, l := range st.leases {
		if !l.live(now) {
			delete(st.leases, id)
		}
	}
	raw, _ := json.Marshal(st.leases)
	if err := writeFileAtomic(filepath.Join(st.root, "leases.json"), raw); err != nil {
		log.Printf("project leases: %v", err)
	}
}

func (st *workspaceStore) leaseConflict(w http.ResponseWriter, id, session string, revision uint64) bool {
	l := st.leaseTable()[id]
	if !l.live(time.Now()) || l.Session == session {
		return false
	}
	leaseFailure(w, l, session, revision)
	return true
}
func leaseFailure(w http.ResponseWriter, l *projectLease, session string, revision uint64) {
	body := workspaceError("lease_held", "This project is open in another tab or device.", "take_control", revision)
	if session != "" && session == l.Previous {
		body = workspaceError("lease_lost", "Another tab or device took control of this project.", "take_control", revision)
	}
	body["holder"] = l.holder()
	writeJSON(w, 409, body)
}

func (st *workspaceStore) claimLease(id, session string) {
	if !validSession(session) {
		return
	}
	now, leases := time.Now(), st.leaseTable()
	if l := leases[id]; l.live(now) {
		return
	}
	leases[id] = &projectLease{Session: session, Since: now.UnixMilli(), LastSeen: now.UnixMilli()}
	st.saveLeases()
}

func (st *workspaceStore) leaseRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /api/project-leases/{id}", func(w http.ResponseWriter, r *http.Request) {
		st.locked(w, func(d *workspaceData) {
			id, err := canonicalWorkspaceProject(d, r.PathValue("id"))
			if err != nil {
				workspaceFailure(w, 404, "project_missing", "Project not found.", "open_projects", 0)
				return
			}
			l, session := st.leaseTable()[id], r.Header.Get(sessionHeader)
			out := map[string]any{"held": l.live(time.Now()), "mine": l.live(time.Now()) && l.Session == session,
				"ttlMs": leaseTTL.Milliseconds(), "heartbeatMs": leaseHeartbeat.Milliseconds()}
			if l.live(time.Now()) {
				out["holder"] = l.holder()
			}
			writeJSON(w, 200, out)
		})
	})
	mux.HandleFunc("POST /api/project-leases/{id}", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			Session  string   `json:"session"`
			Label    string   `json:"label"`
			Take     bool     `json:"take"`
			Previous []string `json:"previous"`
		}
		if !workspaceDecode(w, r, &in) {
			return
		}
		if !validSession(in.Session) || len(in.Label) > 120 {
			workspaceFailure(w, 400, "session_invalid", "A session ID of 8 to 100 letters, digits, '.', '_' or '-' is required.", "reload", 0)
			return
		}
		st.locked(w, func(d *workspaceData) {
			id, err := canonicalWorkspaceProject(d, r.PathValue("id"))
			if p, exists := d.Projects[id]; err != nil || !exists || p.Record == nil {
				workspaceFailure(w, 404, "project_missing", "Project not found.", "open_projects", 0)
				return
			}
			now, leases := time.Now(), st.leaseTable()
			l := leases[id]
			switch {
			case l.live(now) && l.Session == in.Session:
				l.LastSeen = now.UnixMilli()
				if in.Label != "" {
					l.Label = in.Label
				}
			case l.live(now) && len(in.Previous) <= 8 && l.Session != in.Session && slices.Contains(in.Previous, l.Session):
				l.Session, l.LastSeen = in.Session, now.UnixMilli()
				if in.Label != "" {
					l.Label = in.Label
				}
				st.saveLeases()
			case l.live(now) && !in.Take:
				leaseFailure(w, l, in.Session, 0)
				return
			default:
				next := &projectLease{Session: in.Session, Label: in.Label, Since: now.UnixMilli(), LastSeen: now.UnixMilli()}
				if l.live(now) {
					next.Previous = l.Session
				}
				leases[id] = next
				st.saveLeases()
				l = next
			}
			writeJSON(w, 200, map[string]any{"held": true, "mine": true, "holder": l.holder(),
				"ttlMs": leaseTTL.Milliseconds(), "heartbeatMs": leaseHeartbeat.Milliseconds()})
		})
	})
	mux.HandleFunc("POST /api/project-leases/{id}/release", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			Session string `json:"session"`
		}
		raw, _ := io.ReadAll(http.MaxBytesReader(w, r.Body, 4096))
		json.Unmarshal(raw, &in)
		st.locked(w, func(d *workspaceData) {
			id, err := canonicalWorkspaceProject(d, r.PathValue("id"))
			if l := st.leaseTable()[id]; err == nil && l != nil && in.Session != "" && l.Session == in.Session {
				delete(st.leases, id)
				st.saveLeases()
			}
			w.WriteHeader(204)
		})
	})
}
