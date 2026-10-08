// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"crypto/ed25519"
	"database/sql"
	"errors"
	"time"
)

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
