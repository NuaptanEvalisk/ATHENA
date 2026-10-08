// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package relay

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func fixture(t *testing.T, lifetime time.Duration) (*Relay, *httptest.Server, string) {
	t.Helper()
	token := base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{71}, 32))
	r, err := New(Config{token, 1, 1 << 20, lifetime, time.Minute, time.Second})
	if err != nil {
		t.Fatal(err)
	}
	s := httptest.NewTLSServer(r)
	t.Cleanup(func() { r.Close(); s.Close() })
	return r, s, token
}
func allocate(t *testing.T, s *httptest.Server, token string) (Ticket, int) {
	t.Helper()
	req, _ := http.NewRequest("POST", s.URL+"/v1/tickets", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	resp, err := s.Client().Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var ticket Ticket
	if resp.StatusCode == 200 {
		if err := json.NewDecoder(resp.Body).Decode(&ticket); err != nil {
			t.Fatal(err)
		}
	}
	return ticket, resp.StatusCode
}
func TestBidirectionalAndOneUse(t *testing.T) {
	r, s, token := fixture(t, time.Second*5)
	if _, status := allocate(t, s, "wrong"); status != 401 {
		t.Fatal(status)
	}
	ticket, status := allocate(t, s, token)
	if status != 200 || ticket.Endpoints[0] == ticket.Endpoints[1] {
		t.Fatal(status, ticket)
	}
	if _, status := allocate(t, s, token); status != 503 {
		t.Fatal(status)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	dial := func(capability string) (*websocket.Conn, *http.Response, error) {
		headers := http.Header{"Authorization": []string{"Bearer " + token}, "X-Hodarium-Ticket": []string{capability}}
		return websocket.Dial(ctx, "wss"+strings.TrimPrefix(s.URL, "https")+"/v1/stream", &websocket.DialOptions{HTTPClient: s.Client(), HTTPHeader: headers, Subprotocols: []string{"athena-hodarium-stream-v1"}})
	}
	a, _, err := dial(ticket.Endpoints[0])
	if err != nil {
		t.Fatal(err)
	}
	defer a.CloseNow()
	if replay, resp, err := dial(ticket.Endpoints[0]); err == nil {
		replay.CloseNow()
		t.Fatal("replayed ticket accepted")
	} else if resp.StatusCode != 403 {
		t.Fatal(resp.StatusCode)
	}
	b, _, err := dial(ticket.Endpoints[1])
	if err != nil {
		t.Fatal(err)
	}
	defer b.CloseNow()
	x, y := websocket.NetConn(ctx, a, websocket.MessageBinary), websocket.NetConn(ctx, b, websocket.MessageBinary)
	for _, reverse := range []bool{false, true} {
		source, dest := x, y
		if reverse {
			source, dest = y, x
		}
		payload := bytes.Repeat([]byte{0, 255, 17, 93}, 4096)
		if _, err := source.Write(payload); err != nil {
			t.Fatal(err)
		}
		got := make([]byte, len(payload))
		if _, err := io.ReadFull(dest, got); err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(payload, got) {
			t.Fatal("opaque stream changed")
		}
	}
	r.Close()
	if _, err := y.Read(make([]byte, 1)); err == nil {
		t.Fatal("shutdown did not close stream")
	}
}
func TestExpiredReservationReleasesCapacity(t *testing.T) {
	r, s, token := fixture(t, 20*time.Millisecond)
	if _, status := allocate(t, s, token); status != 200 {
		t.Fatal(status)
	}
	deadline := time.Now().Add(time.Second)
	for {
		r.mu.Lock()
		count := len(r.rooms)
		r.mu.Unlock()
		if count == 0 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("reservation did not expire")
		}
		time.Sleep(time.Millisecond)
	}
	if _, status := allocate(t, s, token); status != 200 {
		t.Fatal(status)
	}
}
