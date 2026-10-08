// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"os"
	"testing"
)

// Regenerate the native verifier's wire fixture from the actual Go publisher.
// Only public identities, signed membership and a consumed nonce are exported.
func TestNativeWireFixture(t *testing.T) {
	path := os.Getenv("ATHENA_HODARIUM_WIRE_FIXTURE")
	if path == "" {
		t.Skip("wire fixture generation not requested")
	}
	s, bootstrap := testStore(t)
	initial, err := s.State()
	if err != nil {
		t.Fatal(err)
	}
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
	nonce, signature := proof(t, s, key, "control", member)
	validation, err := s.ValidateMember(context.Background(), member, nonce, signature)
	if err != nil {
		t.Fatal(err)
	}
	var state State
	payload, _ := encoding.DecodeString(validation.State.Payload)
	if err = json.Unmarshal(payload, &state); err != nil {
		t.Fatal(err)
	}
	signState := func(state State) SignedState {
		payload, err := json.Marshal(state)
		if err != nil {
			t.Fatal(err)
		}
		signature := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-STATE-v1\x00"), payload...))
		return SignedState{encoding.EncodeToString(payload), encoding.EncodeToString(signature)}
	}
	conflict := state
	conflict.Epoch = randomToken()
	reusedEpoch := state
	reusedEpoch.Revision++
	fixture := map[string]any{"group": s.Group, "authority": s.PublicKey(),
		"generation": state.Generation, "member": member,
		"public_key": joined.Pending.PublicKey, "nonce": nonce, "validation": validation,
		"initial": initial, "conflicting_state": signState(conflict), "reused_epoch": signState(reusedEpoch)}
	data, err := json.MarshalIndent(fixture, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(path, append(data, '\n'), 0644); err != nil {
		t.Fatal(err)
	}
}
