// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"crypto/ed25519"
	"crypto/tls"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"sync/atomic"
	"testing"
)

// This fixture uses only temporary settings and software test keys. It never
// opens Secret Service or an actual Vault.
func TestNativeRecovery(t *testing.T) {
	binary := os.Getenv("ATHENA_HODARIUM_NATIVE_RECOVERY")
	if binary == "" {
		t.Skip("native recovery fixture not requested")
	}
	root := t.TempDir()
	_, recovery, err := Initialize(filepath.Join(root, "authority"))
	if err != nil {
		t.Fatal(err)
	}
	s, err := Open(filepath.Join(root, "authority"))
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	initial := latestState(t, s).Generation
	seed, _ := encoding.DecodeString(recovery)
	key := ed25519.NewKeyFromSeed(seed)
	recoverTo := func(generation string) error {
		subject := generation + "." + s.PublicKey()
		nonce, err := s.IssueChallenge("recover", subject)
		if err != nil {
			return err
		}
		sig := encoding.EncodeToString(ed25519.Sign(key, ProofMessage(s.Group, "recover", subject, nonce)))
		_, err = s.Recover(RecoveryRequest{generation, nonce, sig})
		return err
	}
	if err := recoverTo(randomToken()); err != nil {
		t.Fatal(err)
	}
	api, err := NewAPI(s, testOrigin)
	if err != nil {
		t.Fatal(err)
	}
	var queries atomic.Int32
	server := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if queries.Add(1) == 2 {
			// The offline key can sign a historical generation after a server
			// restore. The client's durable history must still reject it.
			if err := recoverTo(initial); err != nil {
				http.Error(w, err.Error(), 500)
				return
			}
		}
		api.ServeHTTP(w, r)
	}))
	server.TLS = &tls.Config{MinVersion: tls.VersionTLS13}
	server.StartTLS()
	defer server.Close()
	certificate := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw})
	fixtureNonce := randomToken()
	receipt, err := s.CurrentRecovery(fixtureNonce)
	if err != nil {
		t.Fatal(err)
	}
	config, _ := json.Marshal(map[string]any{"origin": server.URL, "certificate": string(certificate),
		"group": s.Group, "authority": s.PublicKey(), "generation": initial,
		"recovery_public_key": encoding.EncodeToString(s.RecoveryPublic), "identity": randomToken(),
		"proof": receipt, "nonce": fixtureNonce})
	fixture := filepath.Join(root, "fixture.json")
	if err := os.WriteFile(fixture, config, 0600); err != nil {
		t.Fatal(err)
	}
	out, err := exec.Command(binary, fixture).CombinedOutput()
	if err != nil {
		t.Fatalf("native recovery: %v\n%s", err, out)
	}
	t.Log(string(out))
}
