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
	"math"
	"net/http"
)

// All content identifiers are client-derived opaque 32-byte tokens. Do not
// send document names, source UUIDs, text or raw content hashes to this API.
type DecisionRequest struct {
	Operation  string `json:"operation"`
	Member     string `json:"member"`
	Generation string `json:"generation"`
	Epoch      string `json:"epoch"`
	Vault      string `json:"vault"`
	Conflict   string `json:"conflict"`
	Expected   int64  `json:"expected"`
	RequestID  string `json:"request_id,omitempty"`
	Branches   string `json:"branches,omitempty"`
	Resolution string `json:"resolution,omitempty"`
}

type Decision struct {
	Vault      string `json:"vault"`
	Conflict   string `json:"conflict"`
	Version    int64  `json:"version"`
	RequestID  string `json:"request_id"`
	Branches   string `json:"branches"`
	Resolution string `json:"resolution"`
	Member     string `json:"member"`
	Generation string `json:"generation"`
	Epoch      string `json:"epoch"`
	Created    int64  `json:"created"`
}

type DecisionReceipt struct {
	Protocol   int       `json:"protocol"`
	Group      string    `json:"group"`
	Generation string    `json:"generation"`
	Epoch      string    `json:"epoch"`
	Challenge  string    `json:"challenge"`
	Subject    string    `json:"subject"`
	Decision   *Decision `json:"decision"`
}

func DecisionSubject(payload []byte) string {
	hash := sha256.Sum256(payload)
	return encoding.EncodeToString(hash[:])
}

func (s *Store) decide(ctx context.Context, payload []byte, challenge, signature string) (SignedState, error) {
	var in DecisionRequest
	if len(payload) > 4096 {
		return SignedState{}, ErrDenied
	}
	decoder := json.NewDecoder(bytes.NewReader(payload))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&in); err != nil {
		return SignedState{}, ErrDenied
	}
	var extra any
	if decoder.Decode(&extra) != io.EOF {
		return SignedState{}, ErrDenied
	}
	for _, id := range []string{in.Member, in.Generation, in.Epoch, in.Vault, in.Conflict} {
		if _, err := publicKey(id); err != nil {
			return SignedState{}, ErrDenied
		}
	}
	switch in.Operation {
	case "get":
		if in.Expected != 0 || in.RequestID != "" || in.Branches != "" || in.Resolution != "" {
			return SignedState{}, ErrDenied
		}
	case "decide":
		if in.Expected < 0 || in.Expected == math.MaxInt64 {
			return SignedState{}, ErrDenied
		}
		for _, id := range []string{in.RequestID, in.Branches, in.Resolution} {
			if _, err := publicKey(id); err != nil {
				return SignedState{}, ErrDenied
			}
		}
	default:
		return SignedState{}, ErrDenied
	}
	var key string
	if err := s.db.QueryRowContext(ctx, "SELECT public_key FROM members WHERE id=? AND expelled IS NULL", in.Member).Scan(&key); err != nil {
		return SignedState{}, ErrDenied
	}
	if err := s.prove("decision", DecisionSubject(payload), challenge, signature, key); err != nil {
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
	var latest *Decision
	var stored []byte
	err = tx.QueryRow("SELECT payload FROM conflict_decisions WHERE vault=? AND conflict=? ORDER BY version DESC LIMIT 1", in.Vault, in.Conflict).Scan(&stored)
	if err == nil {
		latest = new(Decision)
		if err := json.Unmarshal(stored, latest); err != nil {
			return SignedState{}, err
		}
	} else if !errors.Is(err, sql.ErrNoRows) {
		return SignedState{}, err
	}
	if in.Operation == "decide" {
		var replay []byte
		err = tx.QueryRow("SELECT payload FROM conflict_decisions WHERE operation=?", in.RequestID).Scan(&replay)
		if err == nil {
			var previous Decision
			if err := json.Unmarshal(replay, &previous); err != nil {
				return SignedState{}, err
			}
			if previous.Vault != in.Vault || previous.Conflict != in.Conflict || previous.Version != in.Expected+1 ||
				previous.Branches != in.Branches || previous.Resolution != in.Resolution || previous.Member != in.Member ||
				previous.Generation != in.Generation {
				return SignedState{}, ErrConflict
			}
			// A retry returns the current decision, not a superseded old version.
		} else if errors.Is(err, sql.ErrNoRows) {
			version := int64(0)
			if latest != nil {
				version = latest.Version
				if latest.Branches != in.Branches {
					return SignedState{}, ErrConflict
				}
			}
			if in.Expected != version {
				return SignedState{}, ErrConflict
			}
			var count int
			if err := tx.QueryRow("SELECT COUNT(*) FROM conflict_decisions").Scan(&count); err != nil {
				return SignedState{}, err
			}
			if count >= 100000 {
				return SignedState{}, ErrCapacity
			}
			latest = &Decision{in.Vault, in.Conflict, version + 1, in.RequestID, in.Branches, in.Resolution, in.Member, state.Generation, state.Epoch, s.clock().Unix()}
			encoded, err := json.Marshal(latest)
			if err != nil {
				return SignedState{}, err
			}
			if _, err := tx.Exec("INSERT INTO conflict_decisions VALUES(?,?,?,?,?)", in.Vault, in.Conflict, latest.Version, in.RequestID, encoded); err != nil {
				return SignedState{}, err
			}
			if _, err := tx.Exec("INSERT INTO audit(created,action,actor,target) VALUES(?,?,?,?)", latest.Created, "resolve-conflict", in.Member, in.Conflict); err != nil {
				return SignedState{}, err
			}
		} else {
			return SignedState{}, err
		}
	}
	receipt, err := json.Marshal(DecisionReceipt{1, s.Group, state.Generation, state.Epoch, challenge, DecisionSubject(payload), latest})
	if err != nil {
		return SignedState{}, err
	}
	signed := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-DECISION-v1\x00"), receipt...))
	if err := tx.Commit(); err != nil {
		return SignedState{}, err
	}
	return SignedState{encoding.EncodeToString(receipt), encoding.EncodeToString(signed)}, nil
}

func (a *API) decision(w http.ResponseWriter, r *http.Request) {
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
	out, err := a.store.decide(r.Context(), payload, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
