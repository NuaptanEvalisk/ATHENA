// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"crypto/ed25519"
	"database/sql"
	"encoding/json"
	"errors"
	"net/http"
	"time"
)

// CurrentRecovery binds the offline authorization to a fresh caller nonce and
// to the *current* generation, never to an arbitrary historical event.
func (s *Store) CurrentRecovery(nonce string) (SignedState, error) {
	if _, err := publicKey(nonce); err != nil {
		return SignedState{}, ErrDenied
	}
	var request RecoveryRequest
	var authority string
	err := s.db.QueryRow(`SELECT e.generation,e.challenge,e.signature,e.authority_key
		FROM authority a JOIN recovery_events e ON e.generation=a.generation
		WHERE a.singleton=1`).Scan(&request.Generation, &request.Challenge, &request.Signature, &authority)
	if errors.Is(err, sql.ErrNoRows) {
		return SignedState{}, ErrConflict
	}
	if err != nil {
		return SignedState{}, err
	}
	if authority != s.PublicKey() || !VerifyRecovery(s.Group, authority, s.RecoveryPublic, request) {
		return SignedState{}, ErrDenied
	}
	payload, err := json.Marshal(struct {
		Protocol   int             `json:"protocol"`
		Group      string          `json:"group"`
		Authority  string          `json:"authority"`
		Generation string          `json:"generation"`
		Nonce      string          `json:"nonce"`
		Recovery   RecoveryRequest `json:"recovery"`
	}{1, s.Group, authority, request.Generation, nonce, request})
	if err != nil {
		return SignedState{}, err
	}
	signature := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-RECOVERY-CURRENT-v1\x00"), payload...))
	return SignedState{encoding.EncodeToString(payload), encoding.EncodeToString(signature)}, nil
}

func (a *API) currentRecovery(w http.ResponseWriter, r *http.Request) {
	out, err := a.store.CurrentRecovery(r.PathValue("nonce"))
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}

type RecoveryRequest struct {
	Generation string `json:"generation"`
	Challenge  string `json:"challenge"`
	Signature  string `json:"signature"`
}

type RecoveryResult struct {
	Generation string `json:"generation"`
	Bootstrap  string `json:"bootstrap"`
}

// Recovery deliberately expels all old members. This avoids resurrecting an
// expelled member when the operator restores an old authority database.
func (s *Store) Recover(request RecoveryRequest) (RecoveryResult, error) {
	if _, err := publicKey(request.Generation); err != nil {
		return RecoveryResult{}, err
	}
	subject := request.Generation + "." + s.PublicKey()
	if err := s.prove("recover", subject, request.Challenge, request.Signature, encoding.EncodeToString(s.RecoveryPublic)); err != nil {
		return RecoveryResult{}, err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return RecoveryResult{}, err
	}
	defer tx.Rollback()
	var previous string
	if err = tx.QueryRow("SELECT generation FROM authority").Scan(&previous); err != nil {
		return RecoveryResult{}, err
	}
	if previous == request.Generation {
		return RecoveryResult{}, ErrConflict
	}
	var known int
	err = tx.QueryRow("SELECT 1 FROM recovery_events WHERE generation=?", request.Generation).Scan(&known)
	if err == nil {
		return RecoveryResult{}, ErrConflict
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return RecoveryResult{}, err
	}
	bootstrap := randomToken()
	if _, err = tx.Exec("UPDATE authority SET generation=?,bootstrap_hash=?,bootstrap_expires=?,admin_id=?", request.Generation, tokenHash(bootstrap), s.clock().Add(24*time.Hour).Unix(), randomToken()); err != nil {
		return RecoveryResult{}, err
	}
	if _, err = tx.Exec("UPDATE members SET expelled=? WHERE expelled IS NULL", s.clock().Unix()); err != nil {
		return RecoveryResult{}, err
	}
	if _, err = tx.Exec(`DELETE FROM sessions; DELETE FROM web_challenges;
		DELETE FROM challenges; DELETE FROM credentials; DELETE FROM pending;`); err != nil {
		return RecoveryResult{}, err
	}
	if _, err = tx.Exec("INSERT INTO recovery_events VALUES(?,?,?,?,?)", request.Generation, request.Challenge, request.Signature, s.PublicKey(), s.clock().Unix()); err != nil {
		return RecoveryResult{}, err
	}
	if err = s.publish(tx, "recover-authority", "recovery-key", request.Generation); err != nil {
		return RecoveryResult{}, err
	}
	return RecoveryResult{request.Generation, bootstrap}, tx.Commit()
}

// VerifyRecovery is also useful to inspect a restored authority's public proof.
// A valid historical proof is not sufficient to advance client trust: native
// clients require explicit re-establishment and a fresh authority exchange.
func VerifyRecovery(group, authority string, public ed25519.PublicKey, request RecoveryRequest) bool {
	signature, err := encoding.DecodeString(request.Signature)
	return err == nil && len(public) == ed25519.PublicKeySize && ed25519.Verify(public,
		ProofMessage(group, "recover", request.Generation+"."+authority, request.Challenge), signature)
}
