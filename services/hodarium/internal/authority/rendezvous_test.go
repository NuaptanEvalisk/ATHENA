// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"errors"
	"testing"
	"time"
)

func latestState(t *testing.T, s *Store) State {
	t.Helper()
	signed, err := s.State()
	if err != nil {
		t.Fatal(err)
	}
	bytes, err := encoding.DecodeString(signed.Payload)
	if err != nil {
		t.Fatal(err)
	}
	var state State
	if err := json.Unmarshal(bytes, &state); err != nil {
		t.Fatal(err)
	}
	return state
}
func TestRendezvousProofExpiryAndEpoch(t *testing.T) {
	s, bootstrap := testStore(t)
	_, _, session := enrollAdmin(t, s, bootstrap)
	api, err := NewAPI(s, testOrigin)
	if err != nil {
		t.Fatal(err)
	}
	var keys [2]ed25519.PrivateKey
	var members [2]string
	for i := range keys {
		_, keys[i], err = ed25519.GenerateKey(rand.Reader)
		if err != nil {
			t.Fatal(err)
		}
		joined := joinDevice(t, s, keys[i])
		members[i], err = s.Admit(joined.Pending.ID, joined.Pending.PublicKey, tokenHash(session), int64(i+1))
		if err != nil {
			t.Fatal(err)
		}
	}
	state := latestState(t, s)
	publication := PresenceRequest{Operation: "publish", Member: members[0], Generation: state.Generation, Epoch: state.Epoch,
		Direct: []string{"192.0.2.10:9445"}, Relays: []string{"https://relay.example"}}
	payload, _ := json.Marshal(publication)
	nonce, sig := proof(t, s, keys[0], "rendezvous", RendezvousSubject(payload))
	tampered := publication
	tampered.Direct = []string{"192.0.2.11:9445"}
	changed, _ := json.Marshal(tampered)
	if _, err := api.updatePresence(context.Background(), changed, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("changed addresses accepted: %v", err)
	}
	if _, err := api.updatePresence(context.Background(), payload, nonce, sig); err != nil {
		t.Fatal(err)
	}
	if _, err := api.updatePresence(context.Background(), payload, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("presence replay: %v", err)
	}
	query := PresenceRequest{Operation: "list", Member: members[1], Generation: state.Generation, Epoch: state.Epoch}
	call := func(in PresenceRequest, key ed25519.PrivateKey) (PresencePage, error) {
		data, _ := json.Marshal(in)
		nonce, sig := proof(t, s, key, "rendezvous", RendezvousSubject(data))
		return api.updatePresence(context.Background(), data, nonce, sig)
	}
	page, err := call(query, keys[1])
	if err != nil {
		t.Fatal(err)
	}
	if len(page.Entries) != 1 || page.Entries[0].Member != members[0] || page.Entries[0].Direct[0] != "192.0.2.10:9445" {
		t.Fatal("missing peer presence")
	}
	if _, err := call(query, keys[0]); !errors.Is(err, ErrDenied) {
		t.Fatalf("wrong device proof accepted: %v", err)
	}
	other, err := NewAPI(s, testOrigin)
	if err != nil {
		t.Fatal(err)
	}
	data, _ := json.Marshal(query)
	nonce, sig = proof(t, s, keys[1], "rendezvous", RendezvousSubject(data))
	page, err = other.updatePresence(context.Background(), data, nonce, sig)
	if err != nil || len(page.Entries) != 0 {
		t.Fatal("ephemeral presence survived server restart")
	}
	now := s.clock().Add(91 * time.Second)
	s.clock = func() time.Time { return now }
	page, err = call(query, keys[1])
	if err != nil || len(page.Entries) != 0 {
		t.Fatal("expired presence returned")
	}
	if _, err := call(publication, keys[0]); err != nil {
		t.Fatal(err)
	}
	if err := s.Expel(members[0], tokenHash(session), state.Revision); err != nil {
		t.Fatal(err)
	}
	if _, err := call(query, keys[1]); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale epoch accepted: %v", err)
	}
	state = latestState(t, s)
	query.Epoch = state.Epoch
	page, err = call(query, keys[1])
	if err != nil || len(page.Entries) != 0 {
		t.Fatal("expelled peer remained discoverable")
	}
	publication.Epoch = state.Epoch
	if _, err := call(publication, keys[0]); !errors.Is(err, ErrDenied) {
		t.Fatalf("expelled device published: %v", err)
	}
}
