// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"net/mail"
	"os"
	"strings"
	"time"

	"github.com/resend/resend-go/v4"
)

type notificationConfig struct {
	APIKey string   `json:"resend_api_key"`
	From   string   `json:"from"`
	To     []string `json:"to"`
}

func readNotificationConfig(path string) (notificationConfig, error) {
	var c notificationConfig
	f, err := os.Open(path)
	if err != nil {
		return c, errors.New("cannot open private notification configuration")
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil || !st.Mode().IsRegular() || st.Mode().Perm()&0077 != 0 || st.Size() > 16384 {
		return c, errors.New("notification configuration must be a private regular file of at most 16 KiB")
	}
	d := json.NewDecoder(io.LimitReader(f, 16385))
	d.DisallowUnknownFields()
	if d.Decode(&c) != nil || d.Decode(new(any)) != io.EOF {
		return notificationConfig{}, errors.New("invalid notification configuration JSON")
	}
	if len(c.APIKey) < 8 || len(c.APIKey) > 512 || strings.ContainsAny(c.APIKey, " \t\r\n") ||
		len(c.To) == 0 || len(c.To) > 8 {
		return notificationConfig{}, errors.New("invalid notification configuration fields")
	}
	for _, address := range append([]string{c.From}, c.To...) {
		if len(address) > 320 || strings.ContainsAny(address, "\r\n") {
			return notificationConfig{}, errors.New("invalid notification address")
		}
		if _, err := mail.ParseAddress(address); err != nil {
			return notificationConfig{}, errors.New("invalid notification address")
		}
	}
	return c, nil
}

// Bound provider responses even when they are errors. Never expose SDK error
// text to logs: a provider may echo request headers or configuration values.
type notificationTransport struct{ base http.RoundTripper }
type notificationBody struct {
	io.Reader
	io.Closer
}

func (t notificationTransport) RoundTrip(r *http.Request) (*http.Response, error) {
	response, err := t.base.RoundTrip(r)
	if err == nil {
		response.Body = notificationBody{io.LimitReader(response.Body, 65536), response.Body}
	}
	return response, err
}

type notificationSender func(context.Context, *resend.SendEmailRequest, string) error

func resendNotificationSender(client *http.Client, key string) notificationSender {
	sdk := resend.NewCustomClient(client, key)
	return func(ctx context.Context, message *resend.SendEmailRequest, idempotency string) error {
		response, err := sdk.Emails.SendWithOptions(ctx, message, &resend.SendEmailOptions{IdempotencyKey: idempotency})
		if err != nil {
			return err
		}
		if response == nil || response.Id == "" {
			return errors.New("notification receipt missing")
		}
		return nil
	}
}

type notificationService struct {
	source *Store
	queue  *sql.DB
	config notificationConfig
	send   notificationSender
	now    func() time.Time
}

func openNotifications(s *Store, path string, c notificationConfig, send notificationSender) (*notificationService, error) {
	f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, 0600)
	if err != nil {
		return nil, err
	}
	st, err := f.Stat()
	f.Close()
	if err != nil || !st.Mode().IsRegular() || st.Mode().Perm()&0077 != 0 {
		return nil, errors.New("notification queue must be private")
	}
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	ok := false
	defer func() {
		if !ok {
			db.Close()
		}
	}()
	_, err = db.Exec(`PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;
		CREATE TABLE IF NOT EXISTS checkpoint(singleton INTEGER PRIMARY KEY CHECK(singleton=1),sequence INTEGER NOT NULL,group_id TEXT NOT NULL);
		CREATE TABLE IF NOT EXISTS pending(singleton INTEGER PRIMARY KEY CHECK(singleton=1),sequence INTEGER NOT NULL,
		 request TEXT NOT NULL,idempotency TEXT NOT NULL,first_attempt INTEGER NOT NULL);`)
	if err != nil {
		return nil, err
	}
	var group string
	err = db.QueryRow("SELECT group_id FROM checkpoint WHERE singleton=1").Scan(&group)
	if errors.Is(err, sql.ErrNoRows) {
		var sequence int64
		if err = s.db.QueryRow("SELECT coalesce(max(sequence),0) FROM audit").Scan(&sequence); err != nil {
			return nil, err
		}
		_, err = db.Exec("INSERT INTO checkpoint VALUES(1,?,?)", sequence, s.Group)
	} else if err == nil && group != s.Group {
		return nil, errors.New("notification queue belongs to another authority")
	}
	if err != nil {
		return nil, err
	}
	ok = true
	return &notificationService{s, db, c, send, time.Now}, nil
}

func notificationMessage(c notificationConfig, action string, created int64) *resend.SendEmailRequest {
	label := map[string]string{"admit": "Device admitted", "expel": "Device expelled", "recover-authority": "Authority recovered"}[action]
	text := fmt.Sprintf("ATHENA Hodarium: %s.\nTime (UTC): %s\n\nReview your Hodarium administration panel if this change was unexpected.\n", label, time.Unix(created, 0).UTC().Format(time.RFC3339))
	return &resend.SendEmailRequest{From: c.From, To: c.To, Subject: "ATHENA Hodarium: " + label, Text: text}
}

// Audit is already committed by the authority transaction. This consumer never
// writes that database and never holds its read transaction across network I/O.
func (n *notificationService) step(ctx context.Context) (bool, error) {
	var cursor int64
	if err := n.queue.QueryRowContext(ctx, "SELECT sequence FROM checkpoint WHERE singleton=1").Scan(&cursor); err != nil {
		return false, err
	}
	var sequence, first int64
	var request, key string
	err := n.queue.QueryRowContext(ctx, "SELECT sequence,request,idempotency,first_attempt FROM pending WHERE singleton=1").Scan(&sequence, &request, &key, &first)
	if errors.Is(err, sql.ErrNoRows) {
		var action string
		var created int64
		var tip int64
		if err = n.source.db.QueryRowContext(ctx, "SELECT coalesce(max(sequence),0) FROM audit").Scan(&tip); err != nil {
			return false, err
		}
		if tip < cursor {
			return false, errors.New("notification audit history was rolled back")
		}
		err = n.source.db.QueryRowContext(ctx, "SELECT sequence,action,created FROM audit WHERE sequence>? AND sequence<=? AND action IN('admit','expel','recover-authority') ORDER BY sequence LIMIT 1", cursor, tip).Scan(&sequence, &action, &created)
		if errors.Is(err, sql.ErrNoRows) {
			if tip > cursor {
				_, err = n.queue.ExecContext(ctx, "UPDATE checkpoint SET sequence=? WHERE singleton=1", tip)
				return false, err
			}
			return false, nil
		}
		if err != nil {
			return false, err
		}
		payload, err := json.Marshal(notificationMessage(n.config, action, created))
		if err != nil {
			return false, err
		}
		request = string(payload)
		key = "hodarium/" + randomToken()
		first = n.now().Unix()
		if _, err = n.queue.ExecContext(ctx, "INSERT INTO pending VALUES(1,?,?,?,?)", sequence, request, key, first); err != nil {
			return false, err
		}
	} else if err != nil {
		return false, err
	}
	// Resend retains idempotency keys for 24h. Never blindly retry beyond that
	// window; an earlier timeout could already have delivered the message.
	if n.now().Unix() < first || n.now().Sub(time.Unix(first, 0)) >= 23*time.Hour {
		slog.Warn("Hodarium notification abandoned after its safe retry window", "audit_sequence", sequence)
	} else {
		var message resend.SendEmailRequest
		if err = json.Unmarshal([]byte(request), &message); err != nil {
			return false, err
		}
		attempt, cancel := context.WithTimeout(ctx, 15*time.Second)
		err = n.send(attempt, &message, key)
		cancel()
		if err != nil {
			return false, errors.New("notification delivery deferred")
		}
	}
	tx, err := n.queue.BeginTx(ctx, nil)
	if err != nil {
		return false, err
	}
	defer tx.Rollback()
	if _, err = tx.Exec("UPDATE checkpoint SET sequence=? WHERE singleton=1", sequence); err != nil {
		return false, err
	}
	if _, err = tx.Exec("DELETE FROM pending WHERE singleton=1"); err != nil {
		return false, err
	}
	return true, tx.Commit()
}

// No notification hook is added to Admit/Expel/Recover: their committed audit
// supplies a transactional outbox without coupling authentication to email.
// Stop/join before closing Store. Omission of configuration means no service.
func (s *Store) StartNotifications(parent context.Context, configPath, queuePath string) (func(), error) {
	c, err := readNotificationConfig(configPath)
	if err != nil {
		return nil, err
	}
	httpClient := &http.Client{Timeout: 15 * time.Second, Transport: notificationTransport{http.DefaultTransport},
		CheckRedirect: func(*http.Request, []*http.Request) error { return errors.New("notification redirects prohibited") }}
	n, err := openNotifications(s, queuePath, c, resendNotificationSender(httpClient, c.APIKey))
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithCancel(parent)
	done := make(chan struct{})
	go func() {
		defer close(done)
		defer n.queue.Close()
		failed := false
		for {
			progress, err := n.step(ctx)
			if ctx.Err() != nil {
				return
			}
			delay := 5 * time.Second
			if err != nil {
				if !failed {
					slog.Warn("Hodarium notifications deferred; authentication remains available")
				}
				failed = true
				delay = time.Minute
			} else {
				if failed {
					slog.Info("Hodarium notification service resumed")
				}
				failed = false
				if progress {
					delay = time.Second
				}
			}
			timer := time.NewTimer(delay)
			select {
			case <-ctx.Done():
				timer.Stop()
				return
			case <-timer.C:
			}
		}
	}()
	return func() { cancel(); <-done }, nil
}
