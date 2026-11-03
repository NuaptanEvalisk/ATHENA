// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"database/sql"
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/go-webauthn/webauthn/protocol"
	"github.com/go-webauthn/webauthn/webauthn"
)

type administrator struct {
	id          string
	credentials []webauthn.Credential
	stored      map[string][]byte
}

func (a administrator) WebAuthnID() []byte                         { return []byte(a.id) }
func (a administrator) WebAuthnName() string                       { return "administrator" }
func (a administrator) WebAuthnDisplayName() string                { return "Hodarium administrator" }
func (a administrator) WebAuthnCredentials() []webauthn.Credential { return a.credentials }

func (s *Store) administrator() (administrator, error) {
	a := administrator{stored: map[string][]byte{}}
	if err := s.db.QueryRow("SELECT admin_id FROM authority").Scan(&a.id); err != nil {
		return a, err
	}
	rows, err := s.db.Query("SELECT id,value FROM credentials ORDER BY id")
	if err != nil {
		return a, err
	}
	defer rows.Close()
	for rows.Next() {
		var id string
		var raw []byte
		if err = rows.Scan(&id, &raw); err != nil {
			return a, err
		}
		var c webauthn.Credential
		if err = json.Unmarshal(raw, &c); err != nil {
			return a, err
		}
		a.credentials = append(a.credentials, c)
		a.stored[id] = raw
	}
	return a, rows.Err()
}

type ceremony struct {
	Options any    `json:"options"`
	ID      string `json:"ceremony"`
}

func (s *Store) saveCeremony(kind, authorization string, session *webauthn.SessionData) (string, error) {
	data, err := json.Marshal(session)
	if err != nil {
		return "", err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return "", err
	}
	defer tx.Rollback()
	if _, err = tx.Exec("DELETE FROM web_challenges WHERE expires<=?", s.clock().Unix()); err != nil {
		return "", err
	}
	var count int
	if err = tx.QueryRow("SELECT count(*) FROM web_challenges").Scan(&count); err != nil {
		return "", err
	}
	if count >= 256 {
		return "", ErrCapacity
	}
	id := randomToken()
	_, err = tx.Exec("INSERT INTO web_challenges VALUES(?,?,?,?,?)", tokenHash(id), kind, data, s.clock().Add(2*time.Minute).Unix(), authorization)
	if err != nil {
		return "", err
	}
	return id, tx.Commit()
}

func (s *Store) consumeCeremony(id, kind string) (webauthn.SessionData, string, error) {
	var session webauthn.SessionData
	var raw []byte
	var auth string
	var expires int64
	err := s.db.QueryRow("DELETE FROM web_challenges WHERE hash=? AND kind=? RETURNING value,expires,authorization", tokenHash(id), kind).Scan(&raw, &expires, &auth)
	if errors.Is(err, sql.ErrNoRows) {
		return session, "", ErrDenied
	}
	if err != nil {
		return session, "", err
	}
	if expires <= s.clock().Unix() {
		return session, "", ErrDenied
	}
	err = json.Unmarshal(raw, &session)
	return session, auth, err
}

func (s *Store) session(token string, recent bool) (string, error) {
	var verified, expires int64
	digest := tokenHash(token)
	err := s.db.QueryRow("SELECT verified,expires FROM sessions WHERE hash=?", digest).Scan(&verified, &expires)
	if errors.Is(err, sql.ErrNoRows) {
		return "", ErrDenied
	}
	if err != nil {
		return "", err
	}
	now := s.clock().Unix()
	if verified > now || expires <= now || (recent && now-verified > 300) {
		return "", ErrDenied
	}
	return digest, nil
}

func (s *Store) requireAdmin(tx *sql.Tx, digest string) error {
	var active int
	err := tx.QueryRow("SELECT 1 FROM sessions WHERE hash=? AND expires>? AND verified BETWEEN ? AND ?",
		digest, s.clock().Unix(), s.clock().Unix()-300, s.clock().Unix()).Scan(&active)
	if errors.Is(err, sql.ErrNoRows) {
		return ErrDenied
	}
	return err
}

func (s *Store) beginRegistration(wa *webauthn.WebAuthn, token, bootstrap string) (ceremony, error) {
	a, err := s.administrator()
	if err != nil {
		return ceremony{}, err
	}
	var authorization string
	if len(a.credentials) == 0 {
		var valid int
		err = s.db.QueryRow("SELECT 1 FROM authority WHERE bootstrap_hash=? AND bootstrap_hash<>'' AND bootstrap_expires>?", tokenHash(bootstrap), s.clock().Unix()).Scan(&valid)
		if err != nil {
			return ceremony{}, ErrDenied
		}
		authorization = "bootstrap:" + tokenHash(bootstrap)
	} else {
		digest, err := s.session(token, true)
		if err != nil {
			return ceremony{}, err
		}
		authorization = "session:" + digest
	}
	options, session, err := wa.BeginRegistration(a,
		webauthn.WithExclusions(webauthn.Credentials(a.credentials).CredentialDescriptors()),
		webauthn.WithResidentKeyRequirement(protocol.ResidentKeyRequirementRequired))
	if err != nil {
		return ceremony{}, err
	}
	id, err := s.saveCeremony("registration", authorization, session)
	return ceremony{options, id}, err
}

func (s *Store) finishRegistration(wa *webauthn.WebAuthn, id, sessionToken string, request *http.Request) (string, error) {
	session, authorization, err := s.consumeCeremony(id, "registration")
	if err != nil {
		return "", err
	}
	a, err := s.administrator()
	if err != nil {
		return "", err
	}
	credential, err := wa.FinishRegistration(a, session, request)
	if err != nil {
		return "", ErrDenied
	}
	data, err := json.Marshal(credential)
	if err != nil {
		return "", err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return "", err
	}
	defer tx.Rollback()
	var count int
	if err = tx.QueryRow("SELECT count(*) FROM credentials").Scan(&count); err != nil {
		return "", err
	}
	if count == 0 {
		var digest string
		var expires int64
		if err = tx.QueryRow("SELECT bootstrap_hash,bootstrap_expires FROM authority").Scan(&digest, &expires); err != nil {
			return "", err
		}
		if digest == "" || authorization != "bootstrap:"+digest || expires <= s.clock().Unix() {
			return "", ErrDenied
		}
		if _, err = tx.Exec("UPDATE authority SET bootstrap_hash='',bootstrap_expires=0"); err != nil {
			return "", err
		}
	} else {
		if authorization != "session:"+tokenHash(sessionToken) {
			return "", ErrDenied
		}
		var active int
		err = tx.QueryRow("SELECT 1 FROM sessions WHERE hash=? AND expires>? AND verified BETWEEN ? AND ?", tokenHash(sessionToken), s.clock().Unix(), s.clock().Unix()-300, s.clock().Unix()).Scan(&active)
		if err != nil {
			return "", ErrDenied
		}
	}
	if count >= 32 {
		return "", ErrCapacity
	}
	if _, err = tx.Exec("INSERT INTO credentials VALUES(?,?)", encoding.EncodeToString(credential.ID), data); err != nil {
		return "", err
	}
	if _, err = tx.Exec("INSERT INTO audit(created,action,actor,target) VALUES(?,?,?,?)", s.clock().Unix(), "register-passkey", "administrator", encoding.EncodeToString(credential.ID)); err != nil {
		return "", err
	}
	token, err := s.newSession(tx)
	if err != nil {
		return "", err
	}
	return token, tx.Commit()
}

func (s *Store) beginLogin(wa *webauthn.WebAuthn) (ceremony, error) {
	a, err := s.administrator()
	if err != nil {
		return ceremony{}, err
	}
	if len(a.credentials) == 0 {
		return ceremony{}, ErrDenied
	}
	options, session, err := wa.BeginLogin(a, webauthn.WithUserVerification(protocol.VerificationRequired))
	if err != nil {
		return ceremony{}, err
	}
	id, err := s.saveCeremony("login", "", session)
	return ceremony{options, id}, err
}

func (s *Store) finishLogin(wa *webauthn.WebAuthn, id string, request *http.Request) (string, error) {
	session, _, err := s.consumeCeremony(id, "login")
	if err != nil {
		return "", err
	}
	a, err := s.administrator()
	if err != nil {
		return "", err
	}
	credential, err := wa.FinishLogin(a, session, request)
	if err != nil || credential.Authenticator.CloneWarning {
		return "", ErrDenied
	}
	data, err := json.Marshal(credential)
	if err != nil {
		return "", err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return "", err
	}
	defer tx.Rollback()
	key := encoding.EncodeToString(credential.ID)
	result, err := tx.Exec("UPDATE credentials SET value=? WHERE id=? AND value=?", data, key, a.stored[key])
	if err != nil {
		return "", err
	}
	count, err := result.RowsAffected()
	if err != nil {
		return "", err
	}
	if count != 1 {
		return "", ErrConflict
	}
	token, err := s.newSession(tx)
	if err != nil {
		return "", err
	}
	return token, tx.Commit()
}

func (s *Store) newSession(tx *sql.Tx) (string, error) {
	if _, err := tx.Exec("DELETE FROM sessions WHERE expires<=?", s.clock().Unix()); err != nil {
		return "", err
	}
	var count int
	if err := tx.QueryRow("SELECT count(*) FROM sessions").Scan(&count); err != nil {
		return "", err
	}
	if count >= 128 {
		return "", ErrCapacity
	}
	token := randomToken()
	_, err := tx.Exec("INSERT INTO sessions VALUES(?,?,?)", tokenHash(token), s.clock().Unix(), s.clock().Add(8*time.Hour).Unix())
	return token, err
}
