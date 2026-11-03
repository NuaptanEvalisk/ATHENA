// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/resend/resend-go/v4"
)

type notificationRoundTrip func(*http.Request) (*http.Response, error)

func (f notificationRoundTrip) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func TestNotificationConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "notifications.json")
	data := `{"resend_api_key":"re_FAKE_NEVER_SENT","from":"Hodarium <sender@example.invalid>","to":["owner@example.invalid"]}`
	if err := os.WriteFile(path, []byte(data), 0600); err != nil {
		t.Fatal(err)
	}
	c, err := readNotificationConfig(path)
	if err != nil || len(c.To) != 1 {
		t.Fatal("private config rejected", err)
	}
	if err := os.Chmod(path, 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := readNotificationConfig(path); err == nil {
		t.Fatal("public secret file accepted")
	}
	if err := os.Chmod(path, 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(data+data), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := readNotificationConfig(path); err == nil {
		t.Fatal("trailing config accepted")
	}
}

func TestNotificationsCommittedAuditAndSDK(t *testing.T) {
	s, _ := testStore(t)
	config := notificationConfig{"re_FAKE_NEVER_SENT", "sender@example.invalid", []string{"owner@example.invalid"}}
	path := filepath.Join(t.TempDir(), "notifications.sqlite")
	var requests []string
	var keys []string
	fail := true
	transport := notificationRoundTrip(func(r *http.Request) (*http.Response, error) {
		if r.URL.String() != "https://api.resend.com/emails" || r.Method != "POST" || r.Header.Get("Authorization") != "Bearer "+config.APIKey {
			t.Error("SDK request binding wrong")
		}
		data, _ := io.ReadAll(r.Body)
		requests = append(requests, string(data))
		keys = append(keys, r.Header.Get("Idempotency-Key"))
		if fail {
			return &http.Response{StatusCode: 500, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(`{"message":"re_FAKE_NEVER_SENT"}`))}, nil
		}
		return &http.Response{StatusCode: 200, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(`{"id":"fake-provider-receipt"}`))}, nil
	})
	sender := resendNotificationSender(&http.Client{Transport: notificationTransport{transport}}, config.APIKey)
	n, err := openNotifications(s, path, config, sender)
	if err != nil {
		t.Fatal(err)
	}
	defer func() { n.queue.Close() }()
	ctx := context.Background()
	if progressed, err := n.step(ctx); err != nil || progressed || len(requests) != 0 {
		t.Fatal("historical audit replayed", err)
	}
	tx, err := s.db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	if _, err = tx.Exec("INSERT INTO audit(created,action,actor,target) VALUES(1,'admit','secret-device-key','secret-code')"); err != nil {
		t.Fatal(err)
	}
	if err = tx.Rollback(); err != nil {
		t.Fatal(err)
	}
	if _, err = n.step(ctx); err != nil || len(requests) != 0 {
		t.Fatal("rolled-back admission notified", err)
	}
	for _, action := range []string{"admit", "expel", "recover-authority"} {
		if _, err = s.db.Exec("INSERT INTO audit(created,action,actor,target) VALUES(?,?,?,?)", time.Now().Unix(), action, "secret-device-key", "secret-code-vault-content"); err != nil {
			t.Fatal(err)
		}
	}
	if _, err = n.step(ctx); err == nil || strings.Contains(err.Error(), config.APIKey) {
		t.Fatal("provider failure leaked or was ignored", err)
	}
	var count int
	if err = s.db.QueryRow("SELECT count(*) FROM audit WHERE action IN('admit','expel','recover-authority')").Scan(&count); err != nil || count != 3 {
		t.Fatal("notification failure changed committed audit", err)
	}
	n.queue.Close()
	config.To = []string{"changed@example.invalid"}
	n, err = openNotifications(s, path, config, sender)
	if err != nil {
		t.Fatal(err)
	}
	fail = false
	for range 3 {
		if progressed, err := n.step(ctx); err != nil || !progressed {
			t.Fatal("delivery did not progress", err)
		}
	}
	if len(requests) != 4 || requests[0] != requests[1] || keys[0] == "" || keys[0] != keys[1] {
		t.Fatal("retry lost frozen request or idempotency")
	}
	for i, body := range requests {
		if strings.Contains(body, "secret-") || strings.Contains(body, "re_FAKE") {
			t.Fatal("notification leaked source identity/secret")
		}
		var message resend.SendEmailRequest
		if err = json.Unmarshal([]byte(body), &message); err != nil {
			t.Fatal(err)
		}
		if !strings.HasPrefix(message.Subject, "ATHENA Hodarium:") || message.Text == "" || len(message.Attachments) != 0 {
			t.Fatal("invalid minimal notification")
		}
		if i > 1 && keys[i] == keys[0] {
			t.Fatal("different event reused delivery key")
		}
	}
	if progressed, err := n.step(ctx); err != nil || progressed || len(requests) != 4 {
		t.Fatal("completed notification sent again", err)
	}
	// No retries after the provider's deduplication lifetime can have elapsed.
	if _, err = s.db.Exec("INSERT INTO audit(created,action,actor,target) VALUES(1,'admit','','')"); err != nil {
		t.Fatal(err)
	}
	n.send = func(context.Context, *resend.SendEmailRequest, string) error { return errors.New("temporary failure") }
	if _, err = n.step(ctx); err == nil {
		t.Fatal("failed notification not retained")
	}
	n.now = func() time.Time { return time.Now().Add(24 * time.Hour) }
	n.send = func(context.Context, *resend.SendEmailRequest, string) error {
		t.Fatal("unsafe late retry")
		return nil
	}
	if progressed, err := n.step(ctx); err != nil || !progressed {
		t.Fatal("expired attempt not settled", err)
	}
}
