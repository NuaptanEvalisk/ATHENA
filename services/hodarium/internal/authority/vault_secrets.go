// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"database/sql"
	"encoding/json"
	"errors"
	"io"
	"net/http"
)

// Slot is a domain-separated identifier derived from the shared random Vault
// UUID, not from the candidate secret. Otherwise competing candidates would
// silently register in different slots. No paths or secret bytes enter this API.
type VaultSecretRequest struct {
	Operation  string `json:"operation"`
	Member     string `json:"member"`
	Generation string `json:"generation"`
	Epoch      string `json:"epoch"`
	Slot       string `json:"slot"`
	Commitment string `json:"commitment,omitempty"`
}

type VaultSecretRegistration struct {
	Commitment string `json:"commitment"`
	Member     string `json:"member"`
	Created    int64  `json:"created"`
}

type VaultSecretReceipt struct {
	Protocol     int                      `json:"protocol"`
	Group        string                   `json:"group"`
	Generation   string                   `json:"generation"`
	Epoch        string                   `json:"epoch"`
	Challenge    string                   `json:"challenge"`
	Subject      string                   `json:"subject"`
	Slot         string                   `json:"slot"`
	Registration *VaultSecretRegistration `json:"registration"`
}

func VaultSecretSubject(payload []byte) string {
	hash := sha256.Sum256(payload)
	return encoding.EncodeToString(hash[:])
}

func (s *Store) registerVaultSecret(ctx context.Context, payload []byte, challenge, signature string) (SignedState, error) {
	if len(payload) > 4096 {
		return SignedState{}, ErrDenied
	}
	var in VaultSecretRequest
	decoder := json.NewDecoder(bytes.NewReader(payload))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&in); err != nil {
		return SignedState{}, ErrDenied
	}
	var extra any
	if decoder.Decode(&extra) != io.EOF {
		return SignedState{}, ErrDenied
	}
	for _, id := range []string{in.Member, in.Generation, in.Epoch, in.Slot} {
		if _, err := publicKey(id); err != nil {
			return SignedState{}, ErrDenied
		}
	}
	switch in.Operation {
	case "get":
		if in.Commitment != "" {
			return SignedState{}, ErrDenied
		}
	case "register":
		if _, err := publicKey(in.Commitment); err != nil {
			return SignedState{}, ErrDenied
		}
	default:
		return SignedState{}, ErrDenied
	}
	var key string
	if err := s.db.QueryRowContext(ctx, "SELECT public_key FROM members WHERE id=? AND expelled IS NULL", in.Member).Scan(&key); err != nil {
		return SignedState{}, ErrDenied
	}
	if err := s.prove("vault-secret", VaultSecretSubject(payload), challenge, signature, key); err != nil {
		return SignedState{}, err
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return SignedState{}, err
	}
	defer tx.Rollback()
	var current []byte
	if err := tx.QueryRow("SELECT payload FROM epochs ORDER BY revision DESC LIMIT 1").Scan(&current); err != nil {
		return SignedState{}, err
	}
	var state State
	if err := json.Unmarshal(current, &state); err != nil {
		return SignedState{}, err
	}
	active := false
	for _, member := range state.Members {
		if member.ID == in.Member {
			active = true
		}
	}
	if !active {
		return SignedState{}, ErrDenied
	}
	if in.Generation != state.Generation || in.Epoch != state.Epoch {
		return SignedState{}, ErrConflict
	}
	var registered *VaultSecretRegistration
	entry := new(VaultSecretRegistration)
	err = tx.QueryRow("SELECT commitment,member,created FROM vault_secrets WHERE generation=? AND slot=?", in.Generation, in.Slot).
		Scan(&entry.Commitment, &entry.Member, &entry.Created)
	if err == nil {
		registered = entry
	} else if !errors.Is(err, sql.ErrNoRows) {
		return SignedState{}, err
	}
	// First registration wins. A competing request receives the existing winner,
	// not permission to use its own secret and not an implicit rotation.
	if registered == nil && in.Operation == "register" {
		var count int
		if err := tx.QueryRow("SELECT COUNT(*) FROM vault_secrets").Scan(&count); err != nil {
			return SignedState{}, err
		}
		if count >= 10000 {
			return SignedState{}, ErrCapacity
		}
		registered = &VaultSecretRegistration{in.Commitment, in.Member, s.clock().Unix()}
		if _, err := tx.Exec("INSERT INTO vault_secrets VALUES(?,?,?,?,?)", in.Generation, in.Slot, registered.Commitment, registered.Member, registered.Created); err != nil {
			return SignedState{}, err
		}
		if _, err := tx.Exec("INSERT INTO audit(created,action,actor,target) VALUES(?,?,?,?)", registered.Created, "register-vault-secret", in.Member, in.Slot); err != nil {
			return SignedState{}, err
		}
	}
	receipt, err := json.Marshal(VaultSecretReceipt{1, s.Group, state.Generation, state.Epoch, challenge, VaultSecretSubject(payload), in.Slot, registered})
	if err != nil {
		return SignedState{}, err
	}
	signed := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-VAULT-SECRET-v1\x00"), receipt...))
	if err := tx.Commit(); err != nil {
		return SignedState{}, err
	}
	return SignedState{encoding.EncodeToString(receipt), encoding.EncodeToString(signed)}, nil
}

func (a *API) vaultSecret(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Payload   string `json:"payload"`
		Challenge string `json:"challenge"`
		Signature string `json:"signature"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	if len(in.Payload) > 5500 {
		a.fail(w, ErrDenied)
		return
	}
	payload, err := encoding.Strict().DecodeString(in.Payload)
	if err != nil {
		a.fail(w, ErrDenied)
		return
	}
	out, err := a.store.registerVaultSecret(r.Context(), payload, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
