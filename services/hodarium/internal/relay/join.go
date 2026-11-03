// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package relay

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"time"
)

// A room name is a disposable discovery hint, not a membership credential.
// Both endpoints still authenticate each other inside the forwarded TLS stream.
func (r *Relay) join(w http.ResponseWriter, req *http.Request) {
	var input struct {
		Room string `json:"room"`
		Side *int   `json:"side"`
	}
	decoder := json.NewDecoder(http.MaxBytesReader(w, req.Body, 1024))
	decoder.DisallowUnknownFields()
	if decoder.Decode(&input) != nil || decoder.Decode(new(any)) != io.EOF || input.Side == nil || *input.Side < 0 || *input.Side > 1 {
		http.Error(w, "invalid rendezvous", http.StatusBadRequest)
		return
	}
	decoded, err := base64.RawURLEncoding.Strict().DecodeString(input.Room)
	if err != nil || len(decoded) != 32 {
		http.Error(w, "invalid room", http.StatusBadRequest)
		return
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	p := r.joined[input.Room]
	if r.closed || (p == nil && len(r.rooms) >= r.config.MaxSessions) {
		http.Error(w, "relay capacity unavailable", http.StatusServiceUnavailable)
		return
	}
	if p == nil {
		ctx, cancel := context.WithCancel(context.Background())
		p = &room{ctx: ctx, cancel: cancel, ready: make(chan struct{}), name: input.Room, expires: time.Now().Add(r.config.TicketLifetime).Unix()}
		r.rooms[p] = struct{}{}
		r.joined[input.Room] = p
		p.timer = time.AfterFunc(r.config.TicketLifetime, func() { r.finish(p) })
	}
	side := *input.Side
	if p.issued[side] || p.closed {
		http.Error(w, "endpoint already issued", http.StatusConflict)
		return
	}
	var secret [32]byte
	if _, err := rand.Read(secret[:]); err != nil {
		http.Error(w, "ticket allocation failed", http.StatusInternalServerError)
		return
	}
	token := base64.RawURLEncoding.EncodeToString(secret[:])
	p.tokens[side] = sha256.Sum256([]byte(token))
	p.issued[side] = true
	r.tokens[p.tokens[side]] = endpoint{p, side}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(struct {
		Ticket  string `json:"ticket"`
		Expires int64  `json:"expires"`
	}{token, p.expires})
}
