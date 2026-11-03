// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"encoding/json"
	"io"
	"net/http"
)

type DecisionFeedRequest struct {
	Member     string `json:"member"`
	Generation string `json:"generation"`
	Epoch      string `json:"epoch"`
	After      int64  `json:"after"`
	Limit      int    `json:"limit"`
}
type DecisionFeedEntry struct {
	Sequence int64       `json:"sequence"`
	Receipt  SignedState `json:"receipt"`
}
type DecisionFeedReceipt struct {
	Protocol   int                 `json:"protocol"`
	Group      string              `json:"group"`
	Generation string              `json:"generation"`
	Epoch      string              `json:"epoch"`
	Challenge  string              `json:"challenge"`
	Subject    string              `json:"subject"`
	After      int64               `json:"after"`
	Next       int64               `json:"next"`
	Watermark  int64               `json:"watermark"`
	More       bool                `json:"more"`
	Entries    []DecisionFeedEntry `json:"entries"`
}

func (s *Store) decisionFeed(ctx context.Context, payload []byte, challenge, signature string) (SignedState, error) {
	var in DecisionFeedRequest
	if len(payload) > 2048 {
		return SignedState{}, ErrDenied
	}
	decoder := json.NewDecoder(bytes.NewReader(payload))
	decoder.DisallowUnknownFields()
	if decoder.Decode(&in) != nil || decoder.Decode(new(any)) != io.EOF || in.After < 0 || in.Limit < 1 || in.Limit > 64 {
		return SignedState{}, ErrDenied
	}
	for _, value := range []string{in.Member, in.Generation, in.Epoch} {
		if _, err := publicKey(value); err != nil {
			return SignedState{}, ErrDenied
		}
	}
	var key string
	if err := s.db.QueryRowContext(ctx, "SELECT public_key FROM members WHERE id=? AND expelled IS NULL", in.Member).Scan(&key); err != nil {
		return SignedState{}, ErrDenied
	}
	subject := DecisionSubject(payload)
	if err := s.prove("decision-feed", subject, challenge, signature, key); err != nil {
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
	out := DecisionFeedReceipt{Protocol: 1, Group: s.Group, Generation: state.Generation, Epoch: state.Epoch,
		Challenge: challenge, Subject: subject, After: in.After, Entries: make([]DecisionFeedEntry, 0)}
	if err := tx.QueryRow("SELECT COALESCE(MAX(sequence),0) FROM decision_events WHERE json_extract(payload,'$.generation')=?", state.Generation).Scan(&out.Watermark); err != nil {
		return SignedState{}, err
	}
	if in.After > out.Watermark {
		return SignedState{}, ErrConflict
	}
	// Return only the newest event for each scope. Historical events must not be
	// re-signed as current decision receipts after a newer winner exists.
	rows, err := tx.Query(`SELECT e.sequence,e.payload FROM decision_events e WHERE e.sequence>?
	 AND json_extract(e.payload,'$.generation')=?
	 AND NOT EXISTS(SELECT 1 FROM decision_events n WHERE n.vault=e.vault AND n.conflict=e.conflict
	 AND n.sequence>e.sequence AND json_extract(n.payload,'$.generation')=?)
	 ORDER BY e.sequence LIMIT ?`, in.After, state.Generation, state.Generation, in.Limit)
	if err != nil {
		return SignedState{}, err
	}
	for rows.Next() {
		var sequence int64
		var stored []byte
		if err = rows.Scan(&sequence, &stored); err != nil {
			rows.Close()
			return SignedState{}, err
		}
		var decision Decision
		if err = json.Unmarshal(stored, &decision); err != nil {
			rows.Close()
			return SignedState{}, err
		}
		if decision.Version != 1 {
			rows.Close()
			return SignedState{}, ErrConflict
		}
		encoded, encodeErr := json.Marshal(DecisionReceipt{1, s.Group, state.Generation, state.Epoch, challenge, subject, &decision})
		if encodeErr != nil {
			rows.Close()
			return SignedState{}, encodeErr
		}
		sig := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-DECISION-v1\x00"), encoded...))
		out.Entries = append(out.Entries, DecisionFeedEntry{sequence, SignedState{encoding.EncodeToString(encoded), encoding.EncodeToString(sig)}})
		out.Next = sequence
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return SignedState{}, err
	}
	if len(out.Entries) == 0 {
		out.Next = out.Watermark
	}
	out.More = out.Next < out.Watermark
	encoded, err := json.Marshal(out)
	if err != nil {
		return SignedState{}, err
	}
	sig := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-DECISION-FEED-v1\x00"), encoded...))
	if err := tx.Commit(); err != nil {
		return SignedState{}, err
	}
	return SignedState{encoding.EncodeToString(encoded), encoding.EncodeToString(sig)}, nil
}
func (a *API) decisionFeed(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Payload   string `json:"payload"`
		Challenge string `json:"challenge"`
		Signature string `json:"signature"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	if len(in.Payload) > 2800 {
		a.fail(w, ErrDenied)
		return
	}
	payload, err := encoding.Strict().DecodeString(in.Payload)
	if err != nil {
		a.fail(w, ErrDenied)
		return
	}
	out, err := a.store.decisionFeed(r.Context(), payload, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
