// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package relay

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestNativeRelayServer(t *testing.T) {
	dir := os.Getenv("ATHENA_HODARIUM_NATIVE_RELAY")
	if dir == "" {
		t.Skip("explicit isolated native relay fixture only")
	}
	token := base64.RawURLEncoding.EncodeToString(bytes.Repeat([]byte{37}, 32))
	r, err := New(Config{token, 4, 4 << 20, time.Minute, time.Minute, 10 * time.Second})
	if err != nil {
		t.Fatal(err)
	}
	defer r.Close()
	s := httptest.NewTLSServer(r)
	defer s.Close()
	certificate := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: s.Certificate().Raw})
	data, _ := json.Marshal(map[string]string{"origin": s.URL, "token": token, "certificate": string(certificate)})
	if err := os.WriteFile(filepath.Join(dir, "relay.tmp"), data, 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.Rename(filepath.Join(dir, "relay.tmp"), filepath.Join(dir, "relay.json")); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(35 * time.Second)
	for time.Now().Before(deadline) {
		if _, err := os.Stat(filepath.Join(dir, "done")); err == nil {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("native relay client did not finish")
}
