// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"mime"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/go-webauthn/webauthn/protocol"
	"github.com/go-webauthn/webauthn/webauthn"
)

type API struct {
	store    *Store
	passkeys *webauthn.WebAuthn
	origin   string
	mux      *http.ServeMux
	slots    chan struct{}
}

func NewAPI(store *Store, origin string) (*API, error) {
	u, err := url.Parse(origin)
	if err != nil || u.Scheme != "https" || u.Host == "" || u.User != nil || u.Path != "" || u.RawQuery != "" || u.Fragment != "" {
		return nil, errors.New("origin must be a canonical HTTPS origin, without a path")
	}
	wa, err := webauthn.New(&webauthn.Config{RPID: u.Hostname(), RPOrigins: []string{origin}, RPDisplayName: "ATHENA Hodarium",
		AuthenticatorSelection: protocol.AuthenticatorSelection{UserVerification: protocol.VerificationRequired},
		Timeouts: webauthn.TimeoutsConfig{
			Login:        webauthn.TimeoutConfig{Enforce: true, Timeout: 2 * time.Minute, TimeoutUVD: 2 * time.Minute},
			Registration: webauthn.TimeoutConfig{Enforce: true, Timeout: 2 * time.Minute, TimeoutUVD: 2 * time.Minute}}})
	if err != nil {
		return nil, err
	}
	a := &API{store: store, passkeys: wa, origin: origin, mux: http.NewServeMux(), slots: make(chan struct{}, 32)}
	a.mux.HandleFunc("GET /api/info", a.info)
	a.mux.HandleFunc("POST /api/device/challenge", a.challenge)
	a.mux.HandleFunc("POST /api/device/join", a.join)
	a.mux.HandleFunc("POST /api/device/poll", a.poll)
	a.mux.HandleFunc("POST /api/device/validate", a.validate)
	a.mux.HandleFunc("POST /api/recovery/complete", a.recoverAuthority)
	a.mux.HandleFunc("POST /api/auth/register/start", a.registerStart)
	a.mux.HandleFunc("POST /api/auth/register/finish", a.registerFinish)
	a.mux.HandleFunc("POST /api/auth/login/start", a.loginStart)
	a.mux.HandleFunc("POST /api/auth/login/finish", a.loginFinish)
	a.mux.HandleFunc("POST /api/auth/logout", a.logout)
	a.mux.HandleFunc("GET /api/admin/state", a.adminState)
	a.mux.HandleFunc("POST /api/admin/lookup", a.lookup)
	a.mux.HandleFunc("POST /api/admin/admit", a.admit)
	a.mux.HandleFunc("POST /api/admin/expel", a.expel)
	a.mux.HandleFunc("GET /api/admin/audit", a.audit)
	a.mux.HandleFunc("GET /api/admin/passkeys", a.passkeyList)
	a.mux.HandleFunc("POST /api/admin/passkeys/delete", a.passkeyDelete)
	return a, nil
}

func (s *Store) allow(bucket string, maximum int, period time.Duration) (bool, error) {
	tx, err := s.db.Begin()
	if err != nil {
		return false, err
	}
	defer tx.Rollback()
	now := s.clock().Unix()
	if _, err = tx.Exec("DELETE FROM rate_limits WHERE expires<=?", now); err != nil {
		return false, err
	}
	var size int
	if err = tx.QueryRow("SELECT count(*) FROM rate_limits").Scan(&size); err != nil {
		return false, err
	}
	if size >= 4096 {
		return false, ErrCapacity
	}
	var attempts int
	err = tx.QueryRow(`INSERT INTO rate_limits VALUES(?,?,1) ON CONFLICT(bucket)
		DO UPDATE SET attempts=min(attempts+1,?) RETURNING attempts`, bucket, now+int64(period/time.Second), maximum+1).Scan(&attempts)
	if err != nil {
		return false, err
	}
	return attempts <= maximum, tx.Commit()
}

func (a *API) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Referrer-Policy", "no-referrer")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'")
	select {
	case a.slots <- struct{}{}:
		defer func() { <-a.slots }()
	default:
		a.fail(w, ErrCapacity)
		return
	}
	if r.Header.Get("Sec-Fetch-Site") == "cross-site" {
		a.fail(w, ErrDenied)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/api/auth/") || strings.HasPrefix(r.URL.Path, "/api/admin/") {
		if r.Method != http.MethodGet && r.Header.Get("Origin") != a.origin {
			a.fail(w, ErrDenied)
			return
		}
	}
	if r.Method == http.MethodPost {
		contentType, _, err := mime.ParseMediaType(r.Header.Get("Content-Type"))
		if err != nil || contentType != "application/json" {
			http.Error(w, "application/json required", http.StatusUnsupportedMediaType)
			return
		}
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		a.fail(w, ErrDenied)
		return
	}
	// Forwarded headers are deliberately not trusted to establish client identity.
	allowed, err := a.store.allow("http:"+tokenHash(host), 120, time.Minute)
	if err != nil {
		a.fail(w, err)
		return
	}
	if !allowed {
		a.fail(w, ErrCapacity)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, 128*1024)
	a.mux.ServeHTTP(w, r)
}

func readJSON(r *http.Request, into any) error {
	d := json.NewDecoder(r.Body)
	d.DisallowUnknownFields()
	if err := d.Decode(into); err != nil {
		return ErrDenied
	}
	if err := d.Decode(new(any)); err != io.EOF {
		return ErrDenied
	}
	return nil
}
func respond(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(value); err != nil {
		slog.Warn("control response write failed", "error", err)
	}
}
func (a *API) fail(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, ErrDenied):
		http.Error(w, ErrDenied.Error(), http.StatusForbidden)
	case errors.Is(err, ErrConflict):
		http.Error(w, ErrConflict.Error(), http.StatusConflict)
	case errors.Is(err, ErrCapacity):
		w.Header().Set("Retry-After", "30")
		http.Error(w, ErrCapacity.Error(), http.StatusTooManyRequests)
	default:
		slog.Error("Hodarium control operation failed", "error", err)
		http.Error(w, "control operation failed", http.StatusInternalServerError)
	}
}
func sessionToken(r *http.Request) string {
	c, err := r.Cookie("__Host-hodarium")
	if err != nil {
		return ""
	}
	return c.Value
}
func setSession(w http.ResponseWriter, token string) {
	http.SetCookie(w, &http.Cookie{Name: "__Host-hodarium", Value: token, Secure: true, HttpOnly: true, Path: "/", SameSite: http.SameSiteStrictMode, MaxAge: 8 * 60 * 60})
}
func (a *API) info(w http.ResponseWriter, r *http.Request) {
	var count int
	if err := a.store.db.QueryRow("SELECT count(*) FROM credentials").Scan(&count); err != nil {
		a.fail(w, err)
		return
	}
	respond(w, map[string]any{"protocol": 1, "group": a.store.Group, "authority_key": a.store.PublicKey(), "recovery_public_key": encoding.EncodeToString(a.store.RecoveryPublic), "initialized": count != 0})
}

func (a *API) recoverAuthority(w http.ResponseWriter, r *http.Request) {
	var request RecoveryRequest
	if err := readJSON(r, &request); err != nil {
		a.fail(w, err)
		return
	}
	out, err := a.store.Recover(request)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}

func (a *API) passkeyList(w http.ResponseWriter, r *http.Request) {
	if _, err := a.store.session(sessionToken(r), false); err != nil {
		a.fail(w, err)
		return
	}
	admin, err := a.store.administrator()
	if err != nil {
		a.fail(w, err)
		return
	}
	items := []map[string]any{}
	for _, credential := range admin.credentials {
		items = append(items, map[string]any{"id": encoding.EncodeToString(credential.ID)})
	}
	respond(w, items)
}

func (a *API) passkeyDelete(w http.ResponseWriter, r *http.Request) {
	digest, err := a.store.session(sessionToken(r), true)
	if err != nil {
		a.fail(w, err)
		return
	}
	var in struct {
		ID string `json:"id"`
	}
	if err = readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	tx, err := a.store.db.Begin()
	if err != nil {
		a.fail(w, err)
		return
	}
	defer tx.Rollback()
	if err = a.store.requireAdmin(tx, digest); err != nil {
		a.fail(w, err)
		return
	}
	var count int
	if err = tx.QueryRow("SELECT count(*) FROM credentials").Scan(&count); err != nil {
		a.fail(w, err)
		return
	}
	if count <= 1 {
		a.fail(w, ErrConflict)
		return
	}
	result, err := tx.Exec("DELETE FROM credentials WHERE id=?", in.ID)
	if err != nil {
		a.fail(w, err)
		return
	}
	removed, err := result.RowsAffected()
	if err != nil {
		a.fail(w, err)
		return
	}
	if removed != 1 {
		a.fail(w, ErrConflict)
		return
	}
	if _, err = tx.Exec("DELETE FROM sessions"); err != nil {
		a.fail(w, err)
		return
	}
	if _, err = tx.Exec("INSERT INTO audit(created,action,actor,target) VALUES(?,?,?,?)", a.store.clock().Unix(), "remove-passkey", "administrator", in.ID); err != nil {
		a.fail(w, err)
		return
	}
	if err = tx.Commit(); err != nil {
		a.fail(w, err)
		return
	}
	http.SetCookie(w, &http.Cookie{Name: "__Host-hodarium", Secure: true, HttpOnly: true, Path: "/", SameSite: http.SameSiteStrictMode, MaxAge: -1})
	respond(w, map[string]bool{"removed": true, "authenticated": false})
}
func (a *API) challenge(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Purpose string `json:"purpose"`
		Subject string `json:"subject"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	nonce, err := a.store.IssueChallenge(in.Purpose, in.Subject)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, map[string]string{"challenge": nonce})
}
func (a *API) join(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Name      string `json:"name"`
		PublicKey string `json:"public_key"`
		Challenge string `json:"challenge"`
		Signature string `json:"signature"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	out, err := a.store.Join(in.Name, in.PublicKey, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
func (a *API) poll(w http.ResponseWriter, r *http.Request) {
	var in struct {
		ID         string `json:"id"`
		Credential string `json:"retrieval_credential"`
		Challenge  string `json:"challenge"`
		Signature  string `json:"signature"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	out, err := a.store.Poll(in.ID, in.Credential, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
func (a *API) validate(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Member    string `json:"member"`
		Challenge string `json:"challenge"`
		Signature string `json:"signature"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	out, err := a.store.ValidateMember(r.Context(), in.Member, in.Challenge, in.Signature)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
func (a *API) registerStart(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Bootstrap string `json:"bootstrap"`
	}
	if err := readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	out, err := a.store.beginRegistration(a.passkeys, sessionToken(r), in.Bootstrap)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
func (a *API) registerFinish(w http.ResponseWriter, r *http.Request) {
	token, err := a.store.finishRegistration(a.passkeys, r.Header.Get("X-Hodarium-Ceremony"), sessionToken(r), r)
	if err != nil {
		a.fail(w, err)
		return
	}
	setSession(w, token)
	respond(w, map[string]bool{"authenticated": true})
}
func (a *API) loginStart(w http.ResponseWriter, r *http.Request) {
	out, err := a.store.beginLogin(a.passkeys)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
func (a *API) loginFinish(w http.ResponseWriter, r *http.Request) {
	token, err := a.store.finishLogin(a.passkeys, r.Header.Get("X-Hodarium-Ceremony"), r)
	if err != nil {
		a.fail(w, err)
		return
	}
	setSession(w, token)
	respond(w, map[string]bool{"authenticated": true})
}
func (a *API) logout(w http.ResponseWriter, r *http.Request) {
	_, err := a.store.db.Exec("DELETE FROM sessions WHERE hash=?", tokenHash(sessionToken(r)))
	if err != nil {
		a.fail(w, err)
		return
	}
	http.SetCookie(w, &http.Cookie{Name: "__Host-hodarium", Secure: true, HttpOnly: true, Path: "/", SameSite: http.SameSiteStrictMode, MaxAge: -1})
	respond(w, map[string]bool{"authenticated": false})
}
func (a *API) adminState(w http.ResponseWriter, r *http.Request) {
	if _, err := a.store.session(sessionToken(r), false); err != nil {
		a.fail(w, err)
		return
	}
	state, err := a.store.State()
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, state)
}
func (a *API) lookup(w http.ResponseWriter, r *http.Request) {
	if _, err := a.store.session(sessionToken(r), true); err != nil {
		a.fail(w, err)
		return
	}
	allowed, err := a.store.allow("admission-code", 30, time.Hour)
	if err != nil {
		a.fail(w, err)
		return
	}
	if !allowed {
		a.fail(w, ErrCapacity)
		return
	}
	var in struct {
		Code string `json:"code"`
	}
	if err = readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	out, err := a.store.LookupCode(in.Code)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, out)
}
func (a *API) admit(w http.ResponseWriter, r *http.Request) {
	actor, err := a.store.session(sessionToken(r), true)
	if err != nil {
		a.fail(w, err)
		return
	}
	var in struct {
		ID        string `json:"id"`
		PublicKey string `json:"public_key"`
		Revision  int64  `json:"expected_revision"`
	}
	if err = readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	member, err := a.store.Admit(in.ID, in.PublicKey, actor, in.Revision)
	if err != nil {
		a.fail(w, err)
		return
	}
	respond(w, map[string]string{"member": member})
}
func (a *API) expel(w http.ResponseWriter, r *http.Request) {
	actor, err := a.store.session(sessionToken(r), true)
	if err != nil {
		a.fail(w, err)
		return
	}
	var in struct {
		Member   string `json:"member"`
		Revision int64  `json:"expected_revision"`
	}
	if err = readJSON(r, &in); err != nil {
		a.fail(w, err)
		return
	}
	if err = a.store.Expel(in.Member, actor, in.Revision); err != nil {
		a.fail(w, err)
		return
	}
	respond(w, map[string]bool{"expelled": true})
}
func (a *API) audit(w http.ResponseWriter, r *http.Request) {
	if _, err := a.store.session(sessionToken(r), false); err != nil {
		a.fail(w, err)
		return
	}
	after := int64(0)
	if raw := r.URL.Query().Get("after"); raw != "" {
		var err error
		after, err = strconv.ParseInt(raw, 10, 64)
		if err != nil || after < 0 {
			a.fail(w, ErrDenied)
			return
		}
	}
	rows, err := a.store.db.Query("SELECT sequence,created,action,actor,target FROM audit WHERE sequence>? ORDER BY sequence LIMIT 100", after)
	if err != nil {
		a.fail(w, err)
		return
	}
	defer rows.Close()
	items := []map[string]any{}
	for rows.Next() {
		var sequence, created int64
		var action, actor, target string
		if err = rows.Scan(&sequence, &created, &action, &actor, &target); err != nil {
			a.fail(w, err)
			return
		}
		items = append(items, map[string]any{"sequence": sequence, "created": created, "action": action, "actor": actor, "target": target})
	}
	if err = rows.Err(); err != nil {
		a.fail(w, err)
		return
	}
	respond(w, items)
}
