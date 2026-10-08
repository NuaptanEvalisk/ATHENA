// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package relay

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/json"
	"errors"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/coder/websocket"
)

// Relay credentials control resource use, never Hodarium membership. Payloads
// must already be protected by the peers' mutually authenticated inner transport.
type Config struct {
	AccessToken                                  string
	MaxSessions                                  int
	MaxBytes                                     int64 // per direction
	TicketLifetime, SessionLifetime, IdleTimeout time.Duration
}
type Ticket struct {
	Endpoints [2]string `json:"endpoints"`
	Expires   int64     `json:"expires"`
}
type endpoint struct {
	room  *room
	index int
}
type room struct {
	ctx         context.Context
	cancel      context.CancelFunc
	ready       chan struct{}
	tokens      [2][32]byte
	claimed     [2]bool
	connections [2]net.Conn
	timer       *time.Timer
	closed      bool
}
type Relay struct {
	config Config
	access [32]byte
	mu     sync.Mutex
	rooms  map[*room]struct{}
	tokens map[[32]byte]endpoint
	closed bool
}

func New(c Config) (*Relay, error) {
	key, err := base64.RawURLEncoding.Strict().DecodeString(c.AccessToken)
	if err != nil || len(key) != 32 || c.MaxSessions < 1 || c.MaxBytes < 1 ||
		c.TicketLifetime <= 0 || c.SessionLifetime <= 0 || c.IdleTimeout <= 0 {
		return nil, errors.New("invalid relay credentials or resource limits")
	}
	r := &Relay{config: c, access: sha256.Sum256([]byte(c.AccessToken)), rooms: make(map[*room]struct{}), tokens: make(map[[32]byte]endpoint)}
	r.config.AccessToken = ""
	return r, nil
}
func (r *Relay) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	if req.URL.RawQuery != "" || req.Header.Get("Origin") != "" {
		http.Error(w, "native relay endpoint", http.StatusBadRequest)
		return
	}
	proof := sha256.Sum256([]byte(strings.TrimPrefix(req.Header.Get("Authorization"), "Bearer ")))
	if !strings.HasPrefix(req.Header.Get("Authorization"), "Bearer ") || subtle.ConstantTimeCompare(proof[:], r.access[:]) != 1 {
		http.Error(w, "relay access denied", http.StatusUnauthorized)
		return
	}
	switch {
	case req.Method == "POST" && req.URL.Path == "/v1/tickets":
		r.allocate(w, req)
	case req.Method == "GET" && req.URL.Path == "/v1/stream":
		r.connect(w, req)
	default:
		http.NotFound(w, req)
	}
}
func (r *Relay) allocate(w http.ResponseWriter, req *http.Request) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closed || len(r.rooms) >= r.config.MaxSessions {
		http.Error(w, "relay capacity unavailable", http.StatusServiceUnavailable)
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	p := &room{ctx: ctx, cancel: cancel, ready: make(chan struct{})}
	ticket := Ticket{Expires: time.Now().Add(r.config.TicketLifetime).Unix()}
	for i := range ticket.Endpoints {
		var secret [32]byte
		if _, err := rand.Read(secret[:]); err != nil {
			cancel()
			http.Error(w, "ticket allocation failed", 500)
			return
		}
		ticket.Endpoints[i] = base64.RawURLEncoding.EncodeToString(secret[:])
		p.tokens[i] = sha256.Sum256([]byte(ticket.Endpoints[i]))
	}
	r.rooms[p] = struct{}{}
	for i, hash := range p.tokens {
		r.tokens[hash] = endpoint{p, i}
	}
	p.timer = time.AfterFunc(r.config.TicketLifetime, func() { r.finish(p) })
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(ticket)
}
func (r *Relay) finish(p *room) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if p.closed {
		return
	}
	p.closed = true
	if p.timer != nil {
		p.timer.Stop()
	}
	delete(r.rooms, p)
	for _, hash := range p.tokens {
		delete(r.tokens, hash)
	}
	p.cancel()
}
func (r *Relay) connect(w http.ResponseWriter, req *http.Request) {
	token := req.Header.Get("X-Hodarium-Ticket")
	decoded, err := base64.RawURLEncoding.Strict().DecodeString(token)
	if err != nil || len(decoded) != 32 {
		http.Error(w, "invalid ticket", 403)
		return
	}
	hash := sha256.Sum256([]byte(token))
	r.mu.Lock()
	e, ok := r.tokens[hash]
	if !ok || e.room.closed || e.room.claimed[e.index] {
		r.mu.Unlock()
		http.Error(w, "ticket unavailable", 403)
		return
	}
	p := e.room
	p.claimed[e.index] = true
	delete(r.tokens, hash)
	r.mu.Unlock()
	defer r.finish(p)
	c, err := websocket.Accept(w, req, &websocket.AcceptOptions{Subprotocols: []string{"athena-hodarium-stream-v1"}, CompressionMode: websocket.CompressionDisabled})
	if err != nil {
		return
	}
	defer c.CloseNow()
	if c.Subprotocol() != "athena-hodarium-stream-v1" {
		return
	}
	stream := websocket.NetConn(p.ctx, c, websocket.MessageBinary)
	c.SetReadLimit(64 * 1024)
	r.mu.Lock()
	if p.closed {
		r.mu.Unlock()
		return
	}
	p.connections[e.index] = stream
	if p.connections[0] != nil && p.connections[1] != nil {
		p.timer.Stop()
		p.timer = time.AfterFunc(r.config.SessionLifetime, func() { r.finish(p) })
		close(p.ready)
	}
	r.mu.Unlock()
	select {
	case <-p.ready:
	case <-p.ctx.Done():
		return
	case <-req.Context().Done():
		return
	}
	peer := p.connections[1-e.index]
	buffer := make([]byte, 32*1024)
	remaining := r.config.MaxBytes
	for remaining > 0 {
		_ = stream.SetReadDeadline(time.Now().Add(r.config.IdleTimeout))
		n, err := stream.Read(buffer[:min(int64(len(buffer)), remaining)])
		if n > 0 {
			_ = peer.SetWriteDeadline(time.Now().Add(r.config.IdleTimeout))
			written, writeErr := peer.Write(buffer[:n])
			if writeErr != nil || written != n {
				return
			}
			remaining -= int64(n)
		}
		if err != nil {
			return
		}
	}
}
func (r *Relay) Close() {
	r.mu.Lock()
	r.closed = true
	rooms := make([]*room, 0, len(r.rooms))
	for p := range r.rooms {
		rooms = append(rooms, p)
	}
	r.mu.Unlock()
	for _, p := range rooms {
		r.finish(p)
	}
}
