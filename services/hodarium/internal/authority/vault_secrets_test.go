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

func TestVaultSecretRegistration(t *testing.T) {
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
	slot := VaultSecretSubject([]byte("shared random vault id"))
	request := VaultSecretRequest{Operation: "get", Member: members[0], Generation: state.Generation, Epoch: state.Epoch, Slot: slot}
	type operation struct {
		data             []byte
		nonce, signature string
	}
	prepare := func(in VaultSecretRequest, key ed25519.PrivateKey) operation {
		data, err := json.Marshal(in)
		if err != nil {
			t.Fatal(err)
		}
		nonce, signature := proof(t, s, key, "vault-secret", VaultSecretSubject(data))
		return operation{data, nonce, signature}
	}
	verify := func(result SignedState, op operation) VaultSecretReceipt {
		payload, err := encoding.Strict().DecodeString(result.Payload)
		if err != nil {
			t.Fatal(err)
		}
		signature, err := encoding.Strict().DecodeString(result.Signature)
		if err != nil {
			t.Fatal(err)
		}
		if !ed25519.Verify(s.key.Public().(ed25519.PublicKey), append([]byte("ATHENA-HODARIUM-VAULT-SECRET-v1\x00"), payload...), signature) {
			t.Fatal("bad registration signature")
		}
		var receipt VaultSecretReceipt
		if err := json.Unmarshal(payload, &receipt); err != nil {
			t.Fatal(err)
		}
		if receipt.Protocol != 1 || receipt.Group != s.Group || receipt.Generation != state.Generation || receipt.Epoch != state.Epoch || receipt.Slot != slot || receipt.Challenge != op.nonce || receipt.Subject != VaultSecretSubject(op.data) {
			t.Fatal("unbound receipt")
		}
		return receipt
	}
	op := prepare(request, keys[0])
	result, err := s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature)
	if err != nil {
		t.Fatal(err)
	}
	if verify(result, op).Registration != nil {
		t.Fatal("unexpected initial registration")
	}
	if _, err := s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature); !errors.Is(err, ErrDenied) {
		t.Fatalf("proof replay: %v", err)
	}
	var operations [2]operation
	for i := range keys {
		in := request
		in.Operation = "register"
		in.Member = members[i]
		in.Commitment = encoding.EncodeToString(keys[i].Public().(ed25519.PublicKey))
		operations[i] = prepare(in, keys[i])
	}
	type answer struct {
		op     operation
		result SignedState
		err    error
	}
	answers := make(chan answer, 2)
	for _, op := range operations {
		go func(op operation) {
			value, err := s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature)
			answers <- answer{op, value, err}
		}(op)
	}
	winner := ""
	for range operations {
		got := <-answers
		if got.err != nil {
			t.Fatal(got.err)
		}
		record := verify(got.result, got.op).Registration
		if record == nil {
			t.Fatal("registration not committed")
		}
		if winner == "" {
			winner = record.Commitment
		} else if winner != record.Commitment {
			t.Fatal("competing registrations diverged")
		}
	}
	op = prepare(request, keys[0])
	result, err = s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature)
	if err != nil || verify(result, op).Registration.Commitment != winner {
		t.Fatalf("winner changed: %v", err)
	}
	var count int
	if err := s.db.QueryRow("SELECT COUNT(*) FROM vault_secrets").Scan(&count); err != nil || count != 1 {
		t.Fatalf("registration count=%d err=%v", count, err)
	}
	op = prepare(request, keys[1])
	if _, err := s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature); !errors.Is(err, ErrDenied) {
		t.Fatalf("wrong signing device: %v", err)
	}
	if err := s.Expel(members[0], tokenHash(session), state.Revision); err != nil {
		t.Fatal(err)
	}
	request.Member = members[1]
	op = prepare(request, keys[1])
	if _, err := s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale epoch: %v", err)
	}
	state = latestState(t, s)
	request.Epoch = state.Epoch
	op = prepare(request, keys[1])
	result, err = s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature)
	if err != nil || verify(result, op).Registration.Commitment != winner {
		t.Fatalf("epoch change replaced secret: %v", err)
	}
	request.Member = members[0]
	op = prepare(request, keys[0])
	if _, err := s.registerVaultSecret(context.Background(), op.data, op.nonce, op.signature); !errors.Is(err, ErrDenied) {
		t.Fatalf("expelled device: %v", err)
	}
}
