// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/json"
	"io"
	"net/http"
	"net/netip"
	"net/url"
	"sort"
	"time"
)

type PresenceRequest struct {
	Operation  string   `json:"operation"`
	Member     string   `json:"member"`
	Generation string   `json:"generation"`
	Epoch      string   `json:"epoch"`
	Direct     []string `json:"direct,omitempty"`
	Relays     []string `json:"relays,omitempty"`
	After      string   `json:"after,omitempty"`
}
type Presence struct {
	Member     string   `json:"member"`
	Generation string   `json:"generation"`
	Epoch      string   `json:"epoch"`
	Direct     []string `json:"direct"`
	Relays     []string `json:"relays"`
	Expires    int64    `json:"expires"`
}
type PresencePage struct {
	Generation string     `json:"generation"`
	Epoch      string     `json:"epoch"`
	Entries    []Presence `json:"entries"`
	Next       string     `json:"next,omitempty"`
}

// The proof covers the hash of the exact payload bytes, not only its author.
// This prevents changing addresses, operation or epoch under an old proof.
func RendezvousSubject(payload []byte) string {
	hash := sha256.Sum256(payload)
	return encoding.EncodeToString(hash[:])
}
func validatePresence(in PresenceRequest) error {
	for _, id := range []string{in.Member, in.Generation, in.Epoch} {
		if _, err := publicKey(id); err != nil {
			return ErrDenied
		}
	}
	if in.After != "" {
		if _, err := publicKey(in.After); err != nil {
			return ErrDenied
		}
	}
	switch in.Operation {
	case "list", "withdraw":
		if len(in.Direct) != 0 || len(in.Relays) != 0 || (in.Operation == "withdraw" && in.After != "") {
			return ErrDenied
		}
	case "publish":
		if in.After != "" || len(in.Direct) > 8 || len(in.Relays) > 8 {
			return ErrDenied
		}
		for _, address := range in.Direct {
			endpoint, err := netip.ParseAddrPort(address)
			if err != nil || endpoint.Port() == 0 || endpoint.Addr().Zone() != "" || !endpoint.Addr().IsGlobalUnicast() || endpoint.Addr().IsLoopback() {
				return ErrDenied
			}
		}
		for _, origin := range in.Relays {
			u, err := url.Parse(origin)
			if err != nil || len(origin) > 256 || u.Scheme != "https" || u.Host == "" || u.User != nil || u.Path != "" || u.RawQuery != "" || u.Fragment != "" {
				return ErrDenied
			}
		}
	default:
		return ErrDenied
	}
	return nil
}
func (a *API) updatePresence(ctx context.Context, payload []byte, challenge, signature string) (PresencePage, error) {
	if len(payload) > 16384 {
		return PresencePage{}, ErrDenied
	}
	var in PresenceRequest
	decoder := json.NewDecoder(bytes.NewReader(payload))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&in); err != nil {
		return PresencePage{}, ErrDenied
	}
	var extra any
	if decoder.Decode(&extra) != io.EOF {
		return PresencePage{}, ErrDenied
	}
	if err := validatePresence(in); err != nil {
		return PresencePage{}, err
	}
	var key string
	if err := a.store.db.QueryRowContext(ctx, "SELECT public_key FROM members WHERE id=? AND expelled IS NULL", in.Member).Scan(&key); err != nil {
		return PresencePage{}, ErrDenied
	}
	if err := a.store.prove("rendezvous", RendezvousSubject(payload), challenge, signature, key); err != nil {
		return PresencePage{}, err
	}
	tx, err := a.store.db.BeginTx(ctx, nil)
	if err != nil {
		return PresencePage{}, err
	}
	defer tx.Rollback()
	var current []byte
	if err := tx.QueryRow("SELECT payload FROM epochs ORDER BY revision DESC LIMIT 1").Scan(&current); err != nil {
		return PresencePage{}, err
	}
	var state State
	if err := json.Unmarshal(current, &state); err != nil {
		return PresencePage{}, err
	}
	active := make(map[string]bool, len(state.Members))
	for _, member := range state.Members {
		active[member.ID] = true
	}
	if !active[in.Member] {
		return PresencePage{}, ErrDenied
	}
	if in.Generation != state.Generation || in.Epoch != state.Epoch {
		return PresencePage{}, ErrConflict
	}
	a.presenceMu.Lock()
	defer a.presenceMu.Unlock()
	now := a.store.clock()
	for id, p := range a.presence {
		if p.Expires <= now.Unix() || p.Generation != state.Generation || p.Epoch != state.Epoch || !active[id] {
			delete(a.presence, id)
		}
	}
	switch in.Operation {
	case "publish":
		a.presence[in.Member] = Presence{in.Member, state.Generation, state.Epoch, append([]string{}, in.Direct...), append([]string{}, in.Relays...), now.Add(90 * time.Second).Unix()}
	case "withdraw":
		delete(a.presence, in.Member)
	}
	out := PresencePage{Generation: state.Generation, Epoch: state.Epoch, Entries: []Presence{}}
	if in.Operation == "list" {
		ids := make([]string, 0, len(a.presence))
		for id := range a.presence {
			if id > in.After && id != in.Member {
				ids = append(ids, id)
			}
		}
		sort.Strings(ids)
		for i, id := range ids {
			if i == 64 {
				out.Next = ids[i-1]
				break
			}
			out.Entries = append(out.Entries, a.presence[id])
		}
	}
	return out, tx.Commit()
}
func (a *API) rendezvous(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Payload   string `json:"payload"`
		Challenge string `json:"challenge"`
		Signature string `json:"signature"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	if len(in.Payload) > 22000 {
		a.fail(w, ErrDenied)
		return
	}
	payload, err := encoding.Strict().DecodeString(in.Payload)
	if err != nil {
		a.fail(w, ErrDenied)
		return
	}
	out, err := a.updatePresence(r.Context(), payload, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
