// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"crypto/tls"
	"database/sql"
	"encoding/json"
	"encoding/pem"
	"errors"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestNativeClientServer(t *testing.T) {
	directory := os.Getenv("ATHENA_HODARIUM_NATIVE_CLIENT")
	if directory == "" {
		t.Skip("isolated native client was not requested")
	}
	s, bootstrap := testStore(t)
	_, _, session := enrollAdmin(t, s, bootstrap)
	data, err := os.ReadFile(filepath.Join(directory, "device.json"))
	if err != nil {
		t.Fatal(err)
	}
	var device struct {
		PublicKey string `json:"public_key"`
	}
	if err = json.Unmarshal(data, &device); err != nil {
		t.Fatal(err)
	}
	if _, err = publicKey(device.PublicKey); err != nil {
		t.Fatal(err)
	}
	api, err := NewAPI(s, testOrigin)
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewUnstartedServer(api)
	server.TLS = &tls.Config{MinVersion: tls.VersionTLS13}
	server.StartTLS()
	defer server.Close()
	var generation string
	if err = s.db.QueryRow("SELECT generation FROM authority").Scan(&generation); err != nil {
		t.Fatal(err)
	}
	certificate := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw})
	ready, err := json.Marshal(map[string]any{"origin": server.URL, "certificate": string(certificate),
		"group": s.Group, "authority": s.PublicKey(), "generation": generation,
		"recovery_public_key": encoding.EncodeToString(s.RecoveryPublic)})
	if err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(filepath.Join(directory, "ready.tmp"), ready, 0600); err != nil {
		t.Fatal(err)
	}
	if err = os.Rename(filepath.Join(directory, "ready.tmp"), filepath.Join(directory, "ready.json")); err != nil {
		t.Fatal(err)
	}
	deadline := time.NewTimer(25 * time.Second)
	defer deadline.Stop()
	tick := time.NewTicker(50 * time.Millisecond)
	defer tick.Stop()
	approved := false
	for {
		select {
		case <-tick.C:
			if !approved {
				var id string
				err = s.db.QueryRow("SELECT id FROM pending WHERE public_key=? AND member_id IS NULL", device.PublicKey).Scan(&id)
				if err == nil {
					if _, err = s.Admit(id, device.PublicKey, tokenHash(session), 1); err != nil {
						t.Fatal(err)
					}
					approved = true
				} else if !errors.Is(err, sql.ErrNoRows) {
					t.Fatal(err)
				}
			}
			if _, err = os.Stat(filepath.Join(directory, "done")); err == nil {
				return
			}
		case <-deadline.C:
			t.Fatal("native client did not finish")
		}
	}
}
