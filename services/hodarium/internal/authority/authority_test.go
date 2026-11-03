// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"

	"github.com/fxamacker/cbor/v2"
	"github.com/go-webauthn/webauthn/protocol"
)

const testOrigin = "https://hodarium.example"

func testStore(t *testing.T) (*Store, string) {
	t.Helper()
	directory := filepath.Join(t.TempDir(), "authority")
	bootstrap, recovery, err := Initialize(directory)
	if err != nil {
		t.Fatal(err)
	}
	if recovery == "" {
		t.Fatal("missing recovery seed")
	}
	s, err := Open(directory)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close() })
	return s, bootstrap
}

// A software authenticator exercises the maintained verifier with real P-256
// signatures and CBOR attestation, rather than mocking verification success.
type authenticator struct {
	key     *ecdsa.PrivateKey
	id      []byte
	counter uint32
}

func newAuthenticator(t *testing.T) *authenticator {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	id := make([]byte, 32)
	if _, err = rand.Read(id); err != nil {
		t.Fatal(err)
	}
	return &authenticator{key: key, id: id}
}
func request(value any) *http.Request {
	body, _ := json.Marshal(value)
	r := httptest.NewRequest("POST", testOrigin, bytes.NewReader(body))
	r.Header.Set("Content-Type", "application/json")
	r.Header.Set("Origin", testOrigin)
	return r
}
func (a *authenticator) response(t *testing.T, challenge []byte, registration bool, user []byte, verified bool) *http.Request {
	t.Helper()
	kind := "webauthn.get"
	if registration {
		kind = "webauthn.create"
	}
	client, _ := json.Marshal(map[string]any{"type": kind, "challenge": encoding.EncodeToString(challenge), "origin": testOrigin})
	hash := sha256.Sum256([]byte("hodarium.example"))
	flags := byte(1)
	if verified {
		flags |= 4
	}
	if registration {
		flags |= 64
	}
	data := append(hash[:], flags)
	if !registration {
		a.counter++
	}
	data = binary.BigEndian.AppendUint32(data, a.counter)
	response := map[string]any{"clientDataJSON": encoding.EncodeToString(client)}
	if registration {
		data = append(data, make([]byte, 16)...)
		data = binary.BigEndian.AppendUint16(data, uint16(len(a.id)))
		data = append(data, a.id...)
		pub, err := cbor.Marshal(map[int]any{1: 2, 3: -7, -1: 1, -2: a.key.X.FillBytes(make([]byte, 32)), -3: a.key.Y.FillBytes(make([]byte, 32))})
		if err != nil {
			t.Fatal(err)
		}
		data = append(data, pub...)
		attestation, err := cbor.Marshal(map[string]any{"fmt": "none", "authData": data, "attStmt": map[string]any{}})
		if err != nil {
			t.Fatal(err)
		}
		response["attestationObject"] = encoding.EncodeToString(attestation)
	} else {
		clientHash := sha256.Sum256(client)
		signed := append(append([]byte{}, data...), clientHash[:]...)
		digest := sha256.Sum256(signed)
		signature, err := ecdsa.SignASN1(rand.Reader, a.key, digest[:])
		if err != nil {
			t.Fatal(err)
		}
		response["authenticatorData"] = encoding.EncodeToString(data)
		response["signature"] = encoding.EncodeToString(signature)
		response["userHandle"] = encoding.EncodeToString(user)
	}
	return request(map[string]any{"id": encoding.EncodeToString(a.id), "rawId": encoding.EncodeToString(a.id), "type": "public-key", "response": response, "clientExtensionResults": map[string]any{}})
}

func enrollAdmin(t *testing.T, s *Store, bootstrap string) (*API, *authenticator, string) {
	t.Helper()
	api, err := NewAPI(s, testOrigin)
	if err != nil {
		t.Fatal(err)
	}
	auth := newAuthenticator(t)
	begin, err := s.beginRegistration(api.passkeys, "", bootstrap)
	if err != nil {
		t.Fatal(err)
	}
	options := begin.Options.(*protocol.CredentialCreation)
	token, err := s.finishRegistration(api.passkeys, begin.ID, "", auth.response(t, options.Response.Challenge, true, nil, true))
	if err != nil {
		t.Fatal(err)
	}
	return api, auth, token
}

func TestPasskeyVerificationAndBootstrap(t *testing.T) {
	s, bootstrap := testStore(t)
	api, auth, token := enrollAdmin(t, s, bootstrap)
	if _, err := s.beginRegistration(api.passkeys, "", bootstrap); !errors.Is(err, ErrDenied) {
		t.Fatalf("bootstrap replay: %v", err)
	}
	if _, err := s.session(token, true); err != nil {
		t.Fatal(err)
	}
	admin, err := s.administrator()
	if err != nil {
		t.Fatal(err)
	}
	login, err := s.beginLogin(api.passkeys)
	if err != nil {
		t.Fatal(err)
	}
	options := login.Options.(*protocol.CredentialAssertion)
	r := auth.response(t, options.Response.Challenge, false, admin.WebAuthnID(), true)
	if _, err = s.finishLogin(api.passkeys, login.ID, r); err != nil {
		t.Fatal(err)
	}
	if _, err = s.finishLogin(api.passkeys, login.ID, request(map[string]any{})); !errors.Is(err, ErrDenied) {
		t.Fatalf("ceremony replay: %v", err)
	}
	login, err = s.beginLogin(api.passkeys)
	if err != nil {
		t.Fatal(err)
	}
	options = login.Options.(*protocol.CredentialAssertion)
	if _, err = s.finishLogin(api.passkeys, login.ID, auth.response(t, options.Response.Challenge, false, admin.WebAuthnID(), false)); !errors.Is(err, ErrDenied) {
		t.Fatalf("missing user verification accepted: %v", err)
	}
	login, err = s.beginLogin(api.passkeys)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = s.finishLogin(api.passkeys, login.ID, auth.response(t, []byte("wrong challenge"), false, admin.WebAuthnID(), true)); !errors.Is(err, ErrDenied) {
		t.Fatalf("wrong challenge accepted: %v", err)
	}
}

func proof(t *testing.T, s *Store, key ed25519.PrivateKey, purpose, subject string) (string, string) {
	t.Helper()
	nonce, err := s.IssueChallenge(purpose, subject)
	if err != nil {
		t.Fatal(err)
	}
	return nonce, encoding.EncodeToString(ed25519.Sign(key, ProofMessage(s.Group, purpose, subject, nonce)))
}
func joinDevice(t *testing.T, s *Store, key ed25519.PrivateKey) Admission {
	t.Helper()
	pub := encoding.EncodeToString(key.Public().(ed25519.PublicKey))
	nonce, sig := proof(t, s, key, "join", pub)
	joined, err := s.Join("iPad", pub, nonce, sig)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = s.Join("iPad", pub, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("join replay: %v", err)
	}
	return joined
}

func TestAdmissionEpochExpulsion(t *testing.T) {
	s, bootstrap := testStore(t)
	_, _, session := enrollAdmin(t, s, bootstrap)
	_, key, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	joined := joinDevice(t, s, key)
	p, err := s.LookupCode(joined.Code)
	if err != nil {
		t.Fatal(err)
	}
	if p.ID != joined.Pending.ID {
		t.Fatal("wrong pending request")
	}
	if _, err = s.Admit(p.ID, p.PublicKey, "unauthorized", 1); !errors.Is(err, ErrDenied) {
		t.Fatalf("unauthorized approval: %v", err)
	}
	member, err := s.Admit(p.ID, p.PublicKey, tokenHash(session), 1)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = s.Admit(p.ID, p.PublicKey, tokenHash(session), 2); !errors.Is(err, ErrDenied) {
		t.Fatalf("approval reused: %v", err)
	}
	nonce, sig := proof(t, s, key, "poll", p.ID)
	if _, err = s.Poll(p.ID, joined.Code, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("short code is a credential: %v", err)
	}
	got, err := s.Poll(p.ID, joined.RetrievalCredential, nonce, sig)
	if err != nil {
		t.Fatal(err)
	}
	if got.Pending.MemberID != member {
		t.Fatal("wrong member")
	}
	nonce, sig = proof(t, s, key, "poll", p.ID)
	if _, err = s.Poll(p.ID, joined.RetrievalCredential, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("admission retrieval reused: %v", err)
	}
	nonce, sig = proof(t, s, key, "control", member)
	validation, err := s.ValidateMember(context.Background(), member, nonce, sig)
	if err != nil {
		t.Fatal(err)
	}
	payload, _ := encoding.DecodeString(validation.State.Payload)
	signature, _ := encoding.DecodeString(validation.State.Signature)
	if !ed25519.Verify(s.key.Public().(ed25519.PublicKey), append([]byte("ATHENA-HODARIUM-STATE-v1\x00"), payload...), signature) {
		t.Fatal("invalid state signature")
	}
	var state State
	if err = json.Unmarshal(payload, &state); err != nil {
		t.Fatal(err)
	}
	if state.Revision != 2 || len(state.Members) != 1 || state.Members[0].ID != member {
		t.Fatal("wrong signed membership")
	}
	nonce, sig = proof(t, s, key, "resolve", p.PublicKey)
	resolved, err := s.ResolveDevice(p.PublicKey, nonce, sig)
	if err != nil || resolved.ID != member {
		t.Fatalf("interrupted admission recovery: %v", err)
	}
	if _, err = s.ResolveDevice(p.PublicKey, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("resolve replay: %v", err)
	}
	if err = s.Expel(member, tokenHash(session), 1); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale expulsion: %v", err)
	}
	if err = s.Expel(member, tokenHash(session), 2); err != nil {
		t.Fatal(err)
	}
	nonce, sig = proof(t, s, key, "resolve", p.PublicKey)
	if _, err = s.ResolveDevice(p.PublicKey, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("expelled member resolved: %v", err)
	}
	nonce, sig = proof(t, s, key, "control", member)
	if _, err = s.ValidateMember(context.Background(), member, nonce, sig); !errors.Is(err, ErrDenied) {
		t.Fatalf("expelled member refreshed: %v", err)
	}
	joined = joinDevice(t, s, key)
	newMember, err := s.Admit(joined.Pending.ID, joined.Pending.PublicKey, tokenHash(session), 3)
	if err != nil {
		t.Fatal(err)
	}
	if member == newMember {
		t.Fatal("readmission reused old member instance")
	}
}

func TestCodeBudgetPersistsAcrossRotationAndRestart(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "authority")
	if _, _, err := Initialize(dir); err != nil {
		t.Fatal(err)
	}
	s, err := Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	s.clock = func() time.Time { return now }
	for i := 0; i < 3; i++ {
		ok, err := s.allow("admission-code", 3, time.Hour)
		if err != nil || !ok {
			t.Fatalf("limit: %v", err)
		}
	}
	if err = s.Close(); err != nil {
		t.Fatal(err)
	}
	s, err = Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	s.clock = func() time.Time { return now.Add(31 * time.Second) }
	if ok, err := s.allow("admission-code", 3, time.Hour); err != nil || ok {
		t.Fatalf("rotation/restart reset budget: %v", err)
	}
}

func TestHTTPOriginAndSessionChecks(t *testing.T) {
	s, bootstrap := testStore(t)
	api, _, token := enrollAdmin(t, s, bootstrap)
	r := request(map[string]string{"code": "00000000"})
	r.URL.Path = "/api/admin/lookup"
	r.AddCookie(&http.Cookie{Name: "__Host-hodarium", Value: token})
	r.Header.Set("Origin", "https://attacker.example")
	w := httptest.NewRecorder()
	api.ServeHTTP(w, r)
	if w.Code != http.StatusForbidden {
		t.Fatalf("cross-origin request: %d", w.Code)
	}
	r = httptest.NewRequest("GET", testOrigin+"/api/admin/state", nil)
	w = httptest.NewRecorder()
	api.ServeHTTP(w, r)
	if w.Code != http.StatusForbidden {
		t.Fatalf("unauthenticated member list: %d", w.Code)
	}
}
