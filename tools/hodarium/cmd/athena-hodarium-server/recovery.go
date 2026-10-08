// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package main

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"

	"athena.local/hodarium/internal/authority"
)

func recoverAuthority(arguments []string) error {
	flags := flag.NewFlagSet("recover", flag.ContinueOnError)
	server := flags.String("server", "", "authority HTTPS origin")
	seedFile := flags.String("recovery-seed-file", "", "private offline recovery seed file")
	confirm := flags.Bool("confirm-expel-all", false, "revoke all members and administrator credentials")
	if err := flags.Parse(arguments); err != nil {
		return err
	}
	if flags.NArg() != 0 || *seedFile == "" || !*confirm {
		return errors.New("recover requires --server, --recovery-seed-file and --confirm-expel-all")
	}
	u, err := url.Parse(*server)
	if err != nil || u.Scheme != "https" || u.Host == "" || u.User != nil || u.Path != "" || u.RawQuery != "" || u.Fragment != "" {
		return errors.New("--server must be an HTTPS origin without credentials, path, query or fragment")
	}
	f, err := os.Open(*seedFile)
	if err != nil {
		return err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return err
	}
	if !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 {
		return errors.New("recovery seed must be a private regular file (mode 0600 or 0400)")
	}
	encoded, err := io.ReadAll(io.LimitReader(f, 128))
	if err != nil {
		return err
	}
	defer clear(encoded)
	encoding := base64.RawURLEncoding
	seed, err := encoding.DecodeString(strings.TrimSpace(string(encoded)))
	if err != nil || len(seed) != ed25519.SeedSize {
		return errors.New("invalid recovery seed")
	}
	defer clear(seed)
	key := ed25519.NewKeyFromSeed(seed)
	defer clear(key)
	transport := http.DefaultTransport.(*http.Transport).Clone()
	transport.TLSClientConfig = &tls.Config{MinVersion: tls.VersionTLS13}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 15 * time.Second,
		CheckRedirect: func(*http.Request, []*http.Request) error { return errors.New("authority redirects are not allowed") }}
	var identity struct {
		Protocol  int    `json:"protocol"`
		Group     string `json:"group"`
		Authority string `json:"authority_key"`
		Recovery  string `json:"recovery_public_key"`
	}
	if err = recoveryExchange(client, *server+"/api/info", nil, &identity); err != nil {
		return err
	}
	if identity.Protocol != 1 || identity.Recovery != encoding.EncodeToString(key.Public().(ed25519.PublicKey)) {
		return errors.New("authority protocol or recovery public key does not match")
	}
	generation := make([]byte, 32)
	if _, err = rand.Read(generation); err != nil {
		return err
	}
	request := authority.RecoveryRequest{Generation: encoding.EncodeToString(generation)}
	subject := request.Generation + "." + identity.Authority
	var challenge struct {
		Challenge string `json:"challenge"`
	}
	if err = recoveryExchange(client, *server+"/api/device/challenge", map[string]string{"purpose": "recover", "subject": subject}, &challenge); err != nil {
		return err
	}
	request.Challenge = challenge.Challenge
	request.Signature = encoding.EncodeToString(ed25519.Sign(key, authority.ProofMessage(identity.Group, "recover", subject, request.Challenge)))
	var result authority.RecoveryResult
	if err = recoveryExchange(client, *server+"/api/recovery/complete", request, &result); err != nil {
		return fmt.Errorf("recovery outcome may be unknown; do not restore an older database: %w", err)
	}
	if result.Generation != request.Generation || result.Bootstrap == "" {
		return errors.New("unexpected recovery result")
	}
	fmt.Printf("Recovery generation: %s\nAll old members and administrator credentials revoked.\nBootstrap credential (expires in 24 hours): %s\n", result.Generation, result.Bootstrap)
	return nil
}

func recoveryExchange(client *http.Client, address string, body, result any) error {
	method := http.MethodGet
	var data []byte
	var err error
	if body != nil {
		method = http.MethodPost
		data, err = json.Marshal(body)
		if err != nil {
			return err
		}
	}
	r, err := http.NewRequest(method, address, bytes.NewReader(data))
	if err != nil {
		return err
	}
	if body != nil {
		r.Header.Set("Content-Type", "application/json")
	}
	response, err := client.Do(r)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("authority returned HTTP %d", response.StatusCode)
	}
	const limit = 128 * 1024
	data, err = io.ReadAll(io.LimitReader(response.Body, limit+1))
	if err != nil {
		return err
	}
	if len(data) > limit {
		return errors.New("authority response exceeds limit")
	}
	return json.Unmarshal(data, result)
}
