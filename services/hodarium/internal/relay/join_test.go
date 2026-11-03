// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package relay

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func TestIndependentRendezvous(t *testing.T) {
	r, server, access := fixture(t, 5*time.Second)
	room := base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{91}, 32))
	join := func(side int, token string) (string, int) {
		body, _ := json.Marshal(map[string]any{"room": room, "side": side})
		req, _ := http.NewRequest("POST", server.URL+"/v1/join", bytes.NewReader(body))
		req.Header.Set("Authorization", "Bearer "+token)
		response, err := server.Client().Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer response.Body.Close()
		var ticket struct {
			Ticket  string `json:"ticket"`
			Expires int64  `json:"expires"`
		}
		if response.StatusCode == 200 {
			if err := json.NewDecoder(response.Body).Decode(&ticket); err != nil {
				t.Fatal(err)
			}
			if ticket.Expires <= time.Now().Unix() {
				t.Fatal("expired rendezvous")
			}
		}
		return ticket.Ticket, response.StatusCode
	}
	if _, status := join(0, "invalid"); status != 401 {
		t.Fatal(status)
	}
	if _, status := join(2, access); status != 400 {
		t.Fatal(status)
	}
	a, status := join(0, access)
	if status != 200 {
		t.Fatal(status)
	}
	if _, status := join(0, access); status != 409 {
		t.Fatal(status)
	}
	b, status := join(1, access)
	if status != 200 || a == b {
		t.Fatal(status, "endpoint capabilities must differ")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	dial := func(ticket string) *websocket.Conn {
		headers := http.Header{"Authorization": []string{"Bearer " + access}, "X-Hodarium-Ticket": []string{ticket}}
		connection, _, err := websocket.Dial(ctx, "wss"+strings.TrimPrefix(server.URL, "https")+"/v1/stream", &websocket.DialOptions{
			HTTPClient: server.Client(), HTTPHeader: headers, Subprotocols: []string{"athena-hodarium-stream-v1"},
		})
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { connection.CloseNow() })
		return connection
	}
	x, y := websocket.NetConn(ctx, dial(a), websocket.MessageBinary), websocket.NetConn(ctx, dial(b), websocket.MessageBinary)
	for _, reverse := range []bool{false, true} {
		source, destination := x, y
		if reverse {
			source, destination = y, x
		}
		payload := bytes.Repeat([]byte{19, 0, 77, 255}, 2048)
		if _, err := source.Write(payload); err != nil {
			t.Fatal(err)
		}
		actual := make([]byte, len(payload))
		if _, err := io.ReadFull(destination, actual); err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(actual, payload) {
			t.Fatal("rendezvous changed opaque bytes")
		}
	}
	r.Close()
	r.mu.Lock()
	defer r.mu.Unlock()
	if len(r.joined) != 0 || len(r.tokens) != 0 || len(r.rooms) != 0 {
		t.Fatal("relay retained closed rendezvous")
	}
}
