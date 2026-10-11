// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"bytes"
	"context"
	"crypto/tls"
	"database/sql"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"net/http/httptest"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

// This is the XMLv2 structure of the isolated P2P.before-delete.ath sample,
// including stable source/node identities. No external Vault is read at runtime.
const nativeNodeDocument = `<?xml version="1.0" encoding="UTF-8"?>
<athena-document version="2" text-model="utf-8"><node tag="document"><node tag="style"><node tag="tuple"><text><value>generic</value></text></node></node><node tag="body"><node tag="document" id="29a5648e-f077-477d-8e7c-8d530ef03e66"><text id="09f4a9e8-001b-4516-8468-cf03121fc185"><value>P2P-only arrival from A.</value></text></node></node></node></athena-document>
`

type nativeNodeOutput struct {
	mu sync.Mutex
	b  []byte
}

func (b *nativeNodeOutput) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.b = append(b.b, p...)
	if len(b.b) > 128*1024 {
		b.b = append([]byte(nil), b.b[len(b.b)-128*1024:]...)
	}
	return len(p), nil
}

func (b *nativeNodeOutput) String() string {
	b.mu.Lock()
	defer b.mu.Unlock()
	return string(b.b)
}

type nativeNodeProcess struct {
	cmd    *exec.Cmd
	out    nativeNodeOutput
	done   chan struct{}
	err    error // Published by closing done.
	active bool  // Test goroutine only.
}

func TestNativeNode(t *testing.T) {
	binary := os.Getenv("ATHENA_HODARIUM_NODE_BINARY")
	if binary == "" {
		t.Skip("set ATHENA_HODARIUM_NODE_BINARY to an absolute native node executable")
	}
	info, err := os.Stat(binary)
	if !filepath.IsAbs(binary) || err != nil || !info.Mode().IsRegular() || info.Mode().Perm()&0111 == 0 {
		t.Fatalf("ATHENA_HODARIUM_NODE_BINARY must be an absolute executable: %q (%v)", binary, err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 165*time.Second)
	defer cancel()
	s, bootstrap := testStore(t)
	_, _, adminSession := enrollAdmin(t, s, bootstrap)
	api, err := NewAPI(s, testOrigin)
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewUnstartedServer(api)
	server.TLS = &tls.Config{MinVersion: tls.VersionTLS13}
	server.StartTLS()
	t.Cleanup(server.Close)
	base := t.TempDir()
	ca := filepath.Join(base, "authority.pem")
	if err := os.WriteFile(ca, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw}), 0600); err != nil {
		t.Fatal(err)
	}

	var processes []*nativeNodeProcess
	start := func(state string, args ...string) *nativeNodeProcess {
		t.Helper()
		p := &nativeNodeProcess{done: make(chan struct{}), active: true}
		p.cmd = exec.CommandContext(ctx, binary, append([]string{"--state", state}, args...)...)
		p.cmd.Dir = base
		p.cmd.Env = os.Environ()
		// Keep even incidental Qt/config/cache access away from the user's home.
		for _, key := range []string{"HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_RUNTIME_DIR"} {
			dir := filepath.Join(filepath.Dir(state), strings.ToLower(key))
			if err := os.MkdirAll(dir, 0700); err != nil {
				t.Fatal(err)
			}
			p.cmd.Env = append(p.cmd.Env, key+"="+dir)
		}
		p.cmd.Stdout, p.cmd.Stderr = &p.out, &p.out
		p.cmd.WaitDelay = 2 * time.Second
		if err := p.cmd.Start(); err != nil {
			t.Fatal(err)
		}
		processes = append(processes, p)
		go func() { p.err = p.cmd.Wait(); close(p.done) }()
		return p
	}
	t.Cleanup(func() {
		for _, p := range processes {
			select {
			case <-p.done:
			default:
				_ = p.cmd.Process.Kill()
				<-p.done
			}
			if t.Failed() {
				t.Logf("%v\n%s", p.cmd.Args, p.out.String())
			}
		}
	})
	wait := func(label string, limit time.Duration, ready func() (bool, error)) {
		t.Helper()
		deadline := time.NewTimer(limit)
		defer deadline.Stop()
		tick := time.NewTicker(100 * time.Millisecond)
		defer tick.Stop()
		for {
			ok, err := ready()
			if err != nil {
				t.Fatalf("%s: %v", label, err)
			}
			if ok {
				return
			}
			for _, p := range processes {
				if p.active {
					select {
					case <-p.done:
						t.Fatalf("%s: child exited unexpectedly: %v\n%s", label, p.err, p.out.String())
					default:
					}
				}
			}
			select {
			case <-ctx.Done():
				t.Fatalf("%s: overall native test deadline exceeded", label)
			case <-deadline.C:
				t.Fatalf("%s: timed out after %s", label, limit)
			case <-tick.C:
			}
		}
	}
	exited := func(p *nativeNodeProcess) (bool, error) {
		select {
		case <-p.done:
			p.active = false
			if p.err != nil {
				return false, fmt.Errorf("%v: %w\n%s", p.cmd.Args, p.err, p.out.String())
			}
			return true, nil
		default:
			return false, nil
		}
	}
	run := func(state string, args ...string) {
		t.Helper()
		p := start(state, args...)
		p.active = false // Completion is expected; exited checks its status.
		wait(strings.Join(args, " "), 10*time.Second, func() (bool, error) { return exited(p) })
	}
	stop := func(p *nativeNodeProcess) {
		t.Helper()
		p.active = false
		if err := p.cmd.Process.Signal(os.Interrupt); err != nil {
			t.Fatal(err)
		}
		wait("graceful serve shutdown", 5*time.Second, func() (bool, error) { return exited(p) })
	}
	readDB := func(path string) *sql.DB {
		t.Helper()
		u := url.URL{Scheme: "file", Path: path, RawQuery: "mode=ro"}
		db, err := sql.Open("sqlite", u.String())
		if err != nil {
			t.Fatal(err)
		}
		return db
	}
	type node struct{ state, vault, identity string }
	nodes := make([]node, 2)
	const vaultID = "334c7555-0733-465c-96d6-979e8a586ac5"
	identity := func(state string) string {
		t.Helper()
		db := readDB(filepath.Join(state, "device.sqlite"))
		defer db.Close()
		var handle, key, member string
		if err := db.QueryRow("SELECT key_handle,device_key,member FROM profiles WHERE hodarium=?", s.Group).Scan(&handle, &key, &member); err != nil {
			t.Fatal(err)
		}
		if handle == "" || key == "" || member == "" {
			t.Fatal("incomplete persisted node identity")
		}
		return handle + ":" + key + ":" + member
	}
	for i := range nodes {
		name := fmt.Sprintf("native-node-%d", i)
		n := &nodes[i]
		n.state, n.vault = filepath.Join(base, name, "state"), filepath.Join(base, name, "vault")
		if err := os.MkdirAll(n.state, 0700); err != nil {
			t.Fatal(err)
		}
		run(n.state, "init-vault", "--root", n.vault)
		p := start(n.state, "enroll", "--server", server.URL, "--name", name, "--ca-file", ca)
		wait("enrollment code and pending device", 25*time.Second, func() (bool, error) {
			code := false
			for _, line := range strings.Split(p.out.String(), "\n") {
				var event struct {
					Code string `json:"admission_code"`
				}
				if json.Unmarshal([]byte(line), &event) == nil && event.Code != "" {
					code = true
				}
			}
			if !code {
				return false, nil
			}
			var id, key string
			err := s.db.QueryRow("SELECT id,public_key FROM pending WHERE name=? AND member_id IS NULL", name).Scan(&id, &key)
			if errors.Is(err, sql.ErrNoRows) {
				return false, nil
			}
			if err != nil {
				return false, err
			}
			select {
			case <-p.done:
				return false, fmt.Errorf("enroll exited before approval: %v", p.err)
			default:
			}
			_, err = s.Admit(id, key, tokenHash(adminSession), latestState(t, s).Revision)
			if err == nil {
				p.active = false // Approval permits the enrollment child to exit.
			}
			return err == nil, err
		})
		wait("authorized enrollment exit", 20*time.Second, func() (bool, error) { return exited(p) })
		n.identity = identity(n.state)
		run(n.state, "bind", "--group", s.Group, "--vault", vaultID, "--root", n.vault)
	}
	if nodes[0].identity == nodes[1].identity {
		t.Fatal("independent nodes reused an identity")
	}
	write := func(path, content string) {
		t.Helper()
		if err := os.WriteFile(path+".tmp", []byte(content), 0600); err != nil {
			t.Fatal(err)
		}
		if err := os.Rename(path+".tmp", path); err != nil {
			t.Fatal(err)
		}
	}
	paths := []string{filepath.Join(nodes[0].vault, "P2P.ath"), filepath.Join(nodes[1].vault, "P2P.ath")}
	write(paths[0], nativeNodeDocument)
	serve := func(n node) *nativeNodeProcess { return start(n.state, "serve", "--ca-file", ca) }
	a, b := serve(nodes[0]), serve(nodes[1])
	content := func(path, want string) func() (bool, error) {
		return func() (bool, error) {
			got, err := os.ReadFile(path)
			if errors.Is(err, os.ErrNotExist) {
				return false, nil
			}
			return bytes.Equal(got, []byte(want)), err
		}
	}
	wait("initial P2P native document", 35*time.Second, content(paths[1], nativeNodeDocument))
	updated := strings.Replace(nativeNodeDocument, "P2P-only arrival from A.", "P2P update from A.", 1)
	write(paths[0], updated)
	wait("P2P update", 25*time.Second, content(paths[1], updated))
	if err := os.Remove(paths[0]); err != nil {
		t.Fatal(err)
	}
	wait("P2P deletion", 25*time.Second, func() (bool, error) {
		_, err := os.Stat(paths[1])
		if errors.Is(err, os.ErrNotExist) {
			return true, nil
		}
		return false, err
	})
	stop(a)
	stop(b)
	for i, n := range nodes {
		db := readDB(filepath.Join(n.vault, ".athena", "document-history.sqlite"))
		preimages := []string{updated}
		if i == 1 {
			preimages = append(preimages, nativeNodeDocument)
		}
		for _, preimage := range preimages {
			var count int
			err := db.QueryRow(`SELECT count(*) FROM protected_versions p JOIN versions v ON v.id=p.version_id
WHERE p.source_path='P2P.ath' AND v.encoding=0 AND v.payload=?`, []byte(preimage)).Scan(&count)
			if err != nil || count == 0 {
				db.Close()
				t.Fatalf("node %d lost protected update/deletion preimage: count=%d err=%v", i, count, err)
			}
		}
		db.Close()
	}
	// Successful signed traffic and replication after restart checks usable key
	// persistence, not merely unchanged public fields in the settings database.
	a, b = serve(nodes[0]), serve(nodes[1])
	restarted := strings.Replace(nativeNodeDocument, "P2P-only arrival from A.", "P2P after both nodes restarted.", 1)
	write(paths[1], restarted)
	wait("reverse P2P sync after restart", 30*time.Second, content(paths[0], restarted))
	stop(a)
	stop(b)
	for _, n := range nodes {
		if got := identity(n.state); got != n.identity {
			t.Fatal("node identity changed across restart")
		}
	}
}
