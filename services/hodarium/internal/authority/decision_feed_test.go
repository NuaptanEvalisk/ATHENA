// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"errors"
	"path/filepath"
	"testing"
)

func TestDecisionFeedCursorAndLatestWinner(t *testing.T) {
	s, bootstrap := testStore(t)
	_, _, session := enrollAdmin(t, s, bootstrap)
	_, key, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	joined := joinDevice(t, s, key)
	member, err := s.Admit(joined.Pending.ID, joined.Pending.PublicKey, tokenHash(session), 1)
	if err != nil {
		t.Fatal(err)
	}
	state := latestState(t, s)
	opaque := func(s string) string { return DecisionSubject([]byte(s)) }
	write := func(scope string, version int64) {
		request := DecisionRequest{Operation: "decide", Member: member, Generation: state.Generation, Epoch: state.Epoch,
			Vault: opaque("vault"), Conflict: opaque(scope), Expected: version, RequestID: opaque(scope + string(rune('a'+version))),
			Branches: opaque("branches"), Resolution: opaque("resolution" + string(rune('a'+version)))}
		payload, _ := json.Marshal(request)
		nonce, signature := proof(t, s, key, "decision", DecisionSubject(payload))
		if _, err := s.decide(context.Background(), payload, nonce, signature); err != nil {
			t.Fatal(err)
		}
	}
	write("first", 0)
	write("second", 0)
	request := DecisionFeedRequest{member, state.Generation, state.Epoch, 0, 1}
	read := func(in DecisionFeedRequest) (DecisionFeedReceipt, error) {
		payload, _ := json.Marshal(in)
		nonce, signature := proof(t, s, key, "decision-feed", DecisionSubject(payload))
		result, err := s.decisionFeed(context.Background(), payload, nonce, signature)
		if err != nil {
			return DecisionFeedReceipt{}, err
		}
		decoded, err := encoding.Strict().DecodeString(result.Payload)
		if err != nil {
			t.Fatal(err)
		}
		sig, _ := encoding.Strict().DecodeString(result.Signature)
		if !ed25519.Verify(s.key.Public().(ed25519.PublicKey), append([]byte("ATHENA-HODARIUM-DECISION-FEED-v1\x00"), decoded...), sig) {
			t.Fatal("bad feed signature")
		}
		var out DecisionFeedReceipt
		if err := json.Unmarshal(decoded, &out); err != nil {
			t.Fatal(err)
		}
		if out.Group != s.Group || out.Generation != in.Generation || out.Epoch != in.Epoch || out.Challenge != nonce || out.Subject != DecisionSubject(payload) || out.After != in.After {
			t.Fatal("unbound feed receipt")
		}
		if _, err := s.decisionFeed(context.Background(), payload, nonce, signature); !errors.Is(err, ErrDenied) {
			t.Fatal("replayed feed proof accepted", err)
		}
		return out, nil
	}
	first, err := read(request)
	if err != nil {
		t.Fatal(err)
	}
	if len(first.Entries) != 1 || first.Next != 1 || first.Watermark != 2 || !first.More {
		t.Fatal(first)
	}
	inner, _ := encoding.Strict().DecodeString(first.Entries[0].Receipt.Payload)
	var receipt DecisionReceipt
	if err := json.Unmarshal(inner, &receipt); err != nil {
		t.Fatal(err)
	}
	if receipt.Decision.Version != 1 {
		t.Fatal("noncanonical decision version")
	}
	request.After = first.Next
	second, err := read(request)
	if err != nil || second.Next != 2 || second.More || len(second.Entries) != 1 {
		t.Fatal(second, err)
	}
	request.After = second.Next
	empty, err := read(request)
	if err != nil || empty.Next != 2 || empty.More || len(empty.Entries) != 0 {
		t.Fatal(empty, err)
	}
	write("third", 0)
	updated, err := read(request)
	if err != nil || updated.Next != 3 || len(updated.Entries) != 1 {
		t.Fatal(updated, err)
	}
	request.After = 5
	if _, err := read(request); !errors.Is(err, ErrConflict) {
		t.Fatal("future cursor accepted", err)
	}
	request.After = 0
	request.Epoch = opaque("stale epoch")
	if _, err := read(request); !errors.Is(err, ErrConflict) {
		t.Fatal("stale epoch accepted", err)
	}
	request.Epoch = state.Epoch
	request.Limit = 65
	if _, err := read(request); !errors.Is(err, ErrDenied) {
		t.Fatal("oversize page accepted", err)
	}
	// Simulate an existing control database produced by the old re-adjudication
	// implementation. Neither a feed page nor a direct read may bless its winner.
	legacy := *receipt.Decision
	legacy.Version = 2
	legacy.RequestID = opaque("legacy operation")
	encoded, _ := json.Marshal(legacy)
	if _, err := s.db.Exec("INSERT INTO conflict_decisions VALUES(?,?,?,?,?)", legacy.Vault, legacy.Conflict, 2, legacy.RequestID, encoded); err != nil {
		t.Fatal(err)
	}
	request.After = 3
	request.Limit = 64
	if _, err := read(request); !errors.Is(err, ErrConflict) {
		t.Fatal("legacy re-adjudication feed accepted", err)
	}
	get, _ := json.Marshal(DecisionRequest{Operation: "get", Member: member, Generation: state.Generation, Epoch: state.Epoch, Vault: legacy.Vault, Conflict: legacy.Conflict})
	nonce, signature := proof(t, s, key, "decision", DecisionSubject(get))
	if _, err := s.decide(context.Background(), get, nonce, signature); !errors.Is(err, ErrConflict) {
		t.Fatal("legacy re-adjudication read accepted", err)
	}
}

func TestDecisionFeedRecoveryGeneration(t *testing.T) {
	directory := filepath.Join(t.TempDir(), "authority")
	bootstrap, recovery, err := Initialize(directory)
	if err != nil {
		t.Fatal(err)
	}
	s, err := Open(directory)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	_, _, session := enrollAdmin(t, s, bootstrap)
	_, device, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	admit := func(session string) string {
		joined := joinDevice(t, s, device)
		member, err := s.Admit(joined.Pending.ID, joined.Pending.PublicKey, tokenHash(session), latestState(t, s).Revision)
		if err != nil {
			t.Fatal(err)
		}
		return member
	}
	member := admit(session)
	write := func(scope string) {
		state := latestState(t, s)
		in := DecisionRequest{Operation: "decide", Member: member, Generation: state.Generation, Epoch: state.Epoch,
			Vault: DecisionSubject([]byte("vault")), Conflict: DecisionSubject([]byte(scope)), RequestID: DecisionSubject([]byte(scope + "operation")),
			Branches: DecisionSubject([]byte("branches")), Resolution: DecisionSubject([]byte("resolution"))}
		payload, _ := json.Marshal(in)
		nonce, signature := proof(t, s, device, "decision", DecisionSubject(payload))
		if _, err := s.decide(context.Background(), payload, nonce, signature); err != nil {
			t.Fatal(err)
		}
	}
	write("old generation")
	seed, err := encoding.Strict().DecodeString(recovery)
	if err != nil {
		t.Fatal(err)
	}
	key := ed25519.NewKeyFromSeed(seed)
	generation := randomToken()
	nonce, signature := proof(t, s, key, "recover", generation+"."+s.PublicKey())
	result, err := s.Recover(RecoveryRequest{generation, nonce, signature})
	if err != nil {
		t.Fatal(err)
	}
	_, _, newSession := enrollAdmin(t, s, result.Bootstrap)
	member = admit(newSession)
	read := func(after int64) DecisionFeedReceipt {
		state := latestState(t, s)
		payload, _ := json.Marshal(DecisionFeedRequest{member, state.Generation, state.Epoch, after, 64})
		nonce, signature := proof(t, s, device, "decision-feed", DecisionSubject(payload))
		signed, err := s.decisionFeed(context.Background(), payload, nonce, signature)
		if err != nil {
			t.Fatal(err)
		}
		decoded, err := encoding.Strict().DecodeString(signed.Payload)
		if err != nil {
			t.Fatal(err)
		}
		var page DecisionFeedReceipt
		if err := json.Unmarshal(decoded, &page); err != nil {
			t.Fatal(err)
		}
		return page
	}
	empty := read(0)
	if empty.Watermark != 0 || empty.Next != 0 || empty.More || len(empty.Entries) != 0 {
		t.Fatal("old generation leaked into recovered feed", empty)
	}
	write("new generation")
	page := read(0)
	if page.Watermark != 2 || page.Next != 2 || page.More || len(page.Entries) != 1 || page.Entries[0].Sequence != 2 {
		t.Fatal("recovered feed did not preserve global sequence gap", page)
	}
	decoded, err := encoding.Strict().DecodeString(page.Entries[0].Receipt.Payload)
	if err != nil {
		t.Fatal(err)
	}
	var receipt DecisionReceipt
	if err := json.Unmarshal(decoded, &receipt); err != nil {
		t.Fatal(err)
	}
	if receipt.Generation != generation || receipt.Decision.Generation != generation {
		t.Fatal("recovered receipt has old provenance")
	}
	if next := read(2); next.Next != 2 || next.More || len(next.Entries) != 0 {
		t.Fatal(next)
	}
}
