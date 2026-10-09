// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"errors"
	"testing"
)

func TestDecisionCASAndAuthorization(t *testing.T) {
	s, bootstrap := testStore(t)
	_, _, session := enrollAdmin(t, s, bootstrap)
	var keys [2]ed25519.PrivateKey
	var members [2]string
	for i := range keys {
		_, key, err := ed25519.GenerateKey(rand.Reader)
		if err != nil {
			t.Fatal(err)
		}
		keys[i] = key
		joined := joinDevice(t, s, key)
		members[i], err = s.Admit(joined.Pending.ID, joined.Pending.PublicKey, tokenHash(session), int64(i+1))
		if err != nil {
			t.Fatal(err)
		}
	}
	state := latestState(t, s)
	opaque := func(label string) string { return DecisionSubject([]byte(label)) }
	request := DecisionRequest{Operation: "get", Member: members[0], Generation: state.Generation,
		Epoch: state.Epoch, Vault: opaque("vault"), Conflict: opaque("conflict")}
	call := func(in DecisionRequest, key ed25519.PrivateKey) (DecisionReceipt, error) {
		data, err := json.Marshal(in)
		if err != nil {
			t.Fatal(err)
		}
		nonce, sig := proof(t, s, key, "decision", DecisionSubject(data))
		result, err := s.decide(context.Background(), data, nonce, sig)
		if err != nil {
			return DecisionReceipt{}, err
		}
		payload, err := encoding.Strict().DecodeString(result.Payload)
		if err != nil {
			t.Fatal(err)
		}
		signature, err := encoding.Strict().DecodeString(result.Signature)
		if err != nil {
			t.Fatal(err)
		}
		if !ed25519.Verify(s.key.Public().(ed25519.PublicKey), append([]byte("ATHENA-HODARIUM-DECISION-v1\x00"), payload...), signature) {
			t.Fatal("invalid decision receipt signature")
		}
		var receipt DecisionReceipt
		if err := json.Unmarshal(payload, &receipt); err != nil {
			t.Fatal(err)
		}
		if receipt.Protocol != 1 || receipt.Group != s.Group || receipt.Generation != in.Generation ||
			receipt.Epoch != in.Epoch || receipt.Challenge != nonce || receipt.Subject != DecisionSubject(data) {
			t.Fatal("receipt is not bound to request and membership")
		}
		if _, err := s.decide(context.Background(), data, nonce, sig); !errors.Is(err, ErrDenied) {
			t.Fatalf("replayed proof accepted: %v", err)
		}
		return receipt, nil
	}
	if receipt, err := call(request, keys[0]); err != nil || receipt.Decision != nil {
		t.Fatalf("initial decision: %+v, %v", receipt, err)
	}
	request.Operation = "decide"
	request.RequestID, request.Branches, request.Resolution = opaque("operation1"), opaque("branches"), opaque("resolution1")
	if receipt, err := call(request, keys[0]); err != nil || receipt.Decision.Version != 1 {
		t.Fatalf("first decision: %+v, %v", receipt, err)
	}
	if receipt, err := call(request, keys[0]); err != nil || receipt.Decision.Version != 1 {
		t.Fatalf("idempotent retry: %+v, %v", receipt, err)
	}
	competitor := request
	competitor.Member, competitor.RequestID, competitor.Resolution = members[1], opaque("operation2"), opaque("resolution2")
	if _, err := call(competitor, keys[1]); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale decision version accepted: %v", err)
	}
	competitor.Expected = 1
	wrongBranches := competitor
	wrongBranches.Branches = opaque("different branches")
	if _, err := call(wrongBranches, keys[1]); !errors.Is(err, ErrConflict) {
		t.Fatalf("different conflict branches accepted: %v", err)
	}
	if _, err := call(competitor, keys[0]); !errors.Is(err, ErrDenied) {
		t.Fatalf("wrong device key accepted: %v", err)
	}
	// Both requests observe version 1 before either enters its transaction.
	other := competitor
	other.Member, other.RequestID, other.Resolution = members[0], opaque("operation3"), opaque("resolution3")
	type operation struct {
		data       []byte
		nonce, sig string
	}
	var operations []operation
	for i, in := range []DecisionRequest{other, competitor} {
		data, _ := json.Marshal(in)
		nonce, sig := proof(t, s, keys[i], "decision", DecisionSubject(data))
		operations = append(operations, operation{data, nonce, sig})
	}
	results := make(chan error, 2)
	for _, op := range operations {
		go func(op operation) {
			_, err := s.decide(context.Background(), op.data, op.nonce, op.sig)
			results <- err
		}(op)
	}
	succeeded, conflicted := 0, 0
	for range operations {
		err := <-results
		if err == nil {
			succeeded++
		} else if errors.Is(err, ErrConflict) {
			conflicted++
		} else {
			t.Fatal(err)
		}
	}
	if succeeded != 1 || conflicted != 1 {
		t.Fatalf("CAS results: success=%d conflict=%d", succeeded, conflicted)
	}
	if receipt, err := call(request, keys[0]); err != nil || receipt.Decision.Version != 2 {
		t.Fatalf("old operation retry returned a stale decision: %+v, %v", receipt, err)
	}
	var count int
	if err := s.db.QueryRow("SELECT COUNT(*) FROM conflict_decisions").Scan(&count); err != nil || count != 2 {
		t.Fatalf("unexpected decision log: count=%d err=%v", count, err)
	}
	if current := latestState(t, s); current.Epoch != state.Epoch || current.Revision != state.Revision {
		t.Fatal("content decision changed membership")
	}
	if err := s.Expel(members[0], tokenHash(session), state.Revision); err != nil {
		t.Fatal(err)
	}
	if _, err := call(competitor, keys[1]); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale epoch accepted: %v", err)
	}
	request.Epoch = latestState(t, s).Epoch
	if _, err := call(request, keys[0]); !errors.Is(err, ErrDenied) {
		t.Fatalf("expelled device accepted: %v", err)
	}
}
