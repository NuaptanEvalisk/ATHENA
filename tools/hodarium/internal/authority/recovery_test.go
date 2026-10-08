// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"errors"
	"path/filepath"
	"testing"
)

func TestRecoveryRevokesOldTrust(t *testing.T) {
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
	api, _, session := enrollAdmin(t, s, bootstrap)
	_, device, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	joined := joinDevice(t, s, device)
	member, err := s.Admit(joined.Pending.ID, joined.Pending.PublicKey, tokenHash(session), 1)
	if err != nil {
		t.Fatal(err)
	}
	seed, err := encoding.DecodeString(recovery)
	if err != nil {
		t.Fatal(err)
	}
	key := ed25519.NewKeyFromSeed(seed)
	generation := randomToken()
	subject := generation + "." + s.PublicKey()
	nonce, signature := proof(t, s, device, "recover", subject)
	if _, err = s.Recover(RecoveryRequest{generation, nonce, signature}); !errors.Is(err, ErrDenied) {
		t.Fatalf("device key authorized recovery: %v", err)
	}
	nonce, signature = proof(t, s, key, "recover", subject)
	request := RecoveryRequest{generation, nonce, signature}
	if !VerifyRecovery(s.Group, s.PublicKey(), s.RecoveryPublic, request) {
		t.Fatal("invalid recovery proof")
	}
	result, err := s.Recover(request)
	if err != nil {
		t.Fatal(err)
	}
	if result.Generation != generation || result.Bootstrap == bootstrap {
		t.Fatal("recovery did not rotate credentials")
	}
	if _, err = s.session(session, true); !errors.Is(err, ErrDenied) {
		t.Fatalf("old session survived: %v", err)
	}
	if _, err = s.beginRegistration(api.passkeys, "", bootstrap); !errors.Is(err, ErrDenied) {
		t.Fatalf("old bootstrap survived: %v", err)
	}
	nonce, signature = proof(t, s, device, "control", member)
	if _, err = s.ValidateMember(context.Background(), member, nonce, signature); !errors.Is(err, ErrDenied) {
		t.Fatalf("old member survived: %v", err)
	}
	if _, err = s.Recover(request); !errors.Is(err, ErrDenied) {
		t.Fatalf("recovery replay: %v", err)
	}
	nonce, signature = proof(t, s, key, "recover", subject)
	if _, err = s.Recover(RecoveryRequest{generation, nonce, signature}); !errors.Is(err, ErrConflict) {
		t.Fatalf("generation reused: %v", err)
	}
	_, _, newSession := enrollAdmin(t, s, result.Bootstrap)
	if _, err = s.session(newSession, true); err != nil {
		t.Fatal(err)
	}
	var revision, members int
	var current string
	if err = s.db.QueryRow("SELECT revision,generation FROM authority").Scan(&revision, &current); err != nil {
		t.Fatal(err)
	}
	if err = s.db.QueryRow("SELECT count(*) FROM members WHERE expelled IS NULL").Scan(&members); err != nil {
		t.Fatal(err)
	}
	if revision != 3 || current != generation || members != 0 {
		t.Fatal("incorrect recovered membership")
	}
}
