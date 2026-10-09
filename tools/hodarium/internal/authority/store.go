// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package authority

import (
	"context"
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	_ "modernc.org/sqlite"
)

var ErrDenied = errors.New("request expired, invalid or not authorized")
var ErrConflict = errors.New("control state changed; refresh before retrying")
var ErrCapacity = errors.New("pending request capacity reached")

var encoding = base64.RawURLEncoding

type Store struct {
	db             *sql.DB
	key            ed25519.PrivateKey
	Group          string
	RecoveryPublic ed25519.PublicKey
	clock          func() time.Time
}

type Member struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	PublicKey string `json:"public_key"`
}
type State struct {
	Protocol   int      `json:"protocol"`
	Group      string   `json:"group"`
	Generation string   `json:"generation"`
	Revision   int64    `json:"revision"`
	Epoch      string   `json:"epoch"`
	Members    []Member `json:"members"`
}

// Payload is encoded verbatim. Verifiers verify before parsing, never reserialize.
type SignedState struct {
	Payload   string `json:"payload"`
	Signature string `json:"signature"`
}
type Pending struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	PublicKey string `json:"public_key"`
	Expires   int64  `json:"expires"`
	MemberID  string `json:"member_id,omitempty"`
}
type Admission struct {
	Pending             Pending `json:"request"`
	RetrievalCredential string  `json:"retrieval_credential,omitempty"`
	Code                string  `json:"code,omitempty"`
	CodeExpires         int64   `json:"code_expires,omitempty"`
}

func randomToken() string {
	var b [32]byte
	if _, err := rand.Read(b[:]); err != nil {
		panic(err)
	}
	return encoding.EncodeToString(b[:])
}
func tokenHash(token string) string {
	digest := sha256.Sum256([]byte(token))
	return hex.EncodeToString(digest[:])
}
func publicKey(encoded string) (ed25519.PublicKey, error) {
	key, err := encoding.DecodeString(encoded)
	if err != nil || len(key) != ed25519.PublicKeySize || encoding.EncodeToString(key) != encoded {
		return nil, ErrDenied
	}
	return ed25519.PublicKey(key), nil
}

// Initialize is a local administrator action, never an HTTP first-visitor flow.
// Recovery secret and bootstrap credential are returned once to the operator.
func Initialize(directory string) (bootstrap, recovery string, err error) {
	if err = os.Mkdir(directory, 0700); err != nil {
		return
	}
	pub, key, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return "", "", err
	}
	recoveryPub, recoveryKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return "", "", err
	}
	keyFile, err := os.OpenFile(filepath.Join(directory, "identity.key"), os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
	if err != nil {
		return "", "", err
	}
	_, err = keyFile.Write(key)
	if err == nil {
		err = keyFile.Sync()
	}
	closeErr := keyFile.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		return "", "", err
	}
	db, err := openDatabase(directory)
	if err != nil {
		return "", "", err
	}
	defer db.Close()
	tx, err := db.Begin()
	if err != nil {
		return "", "", err
	}
	defer tx.Rollback()
	bootstrap = randomToken()
	group, generation := randomToken(), randomToken()
	_, err = tx.Exec(`INSERT INTO authority VALUES(1,?,?,?,?,?,?,?,0)`, group, generation,
		encoding.EncodeToString(pub), encoding.EncodeToString(recoveryPub), tokenHash(bootstrap),
		time.Now().Add(24*time.Hour).Unix(), randomToken())
	if err != nil {
		return "", "", err
	}
	s := &Store{db: db, key: key, Group: group, clock: time.Now}
	if err = s.publish(tx, "bootstrap", "local", ""); err != nil {
		return "", "", err
	}
	if err = tx.Commit(); err != nil {
		return "", "", err
	}
	dir, err := os.Open(directory)
	if err != nil {
		return "", "", err
	}
	err = dir.Sync()
	dir.Close()
	return bootstrap, encoding.EncodeToString(recoveryKey.Seed()), err
}

func openDatabase(directory string) (*sql.DB, error) {
	info, err := os.Stat(directory)
	if err != nil {
		return nil, err
	}
	if !info.IsDir() || info.Mode().Perm()&0077 != 0 {
		return nil, errors.New("authority directory must be private (mode 0700)")
	}
	db, err := sql.Open("sqlite", filepath.Join(directory, "control.sqlite"))
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	if _, err = db.Exec(`PRAGMA foreign_keys=ON; PRAGMA busy_timeout=5000;
		PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;`); err != nil {
		db.Close()
		return nil, err
	}
	var version int
	if err = db.QueryRow("PRAGMA user_version").Scan(&version); err != nil {
		db.Close()
		return nil, err
	}
	if version > 4 {
		db.Close()
		return nil, errors.New("unsupported Hodarium control database version")
	}
	if version == 0 {
		_, err = db.Exec(`BEGIN IMMEDIATE;
		CREATE TABLE authority(singleton INTEGER PRIMARY KEY CHECK(singleton=1),
		 group_id TEXT NOT NULL,generation TEXT NOT NULL,public_key TEXT NOT NULL,
		 recovery_public TEXT NOT NULL,bootstrap_hash TEXT NOT NULL,bootstrap_expires INTEGER NOT NULL,
		 admin_id TEXT NOT NULL,revision INTEGER NOT NULL);
		CREATE TABLE members(id TEXT PRIMARY KEY,name TEXT NOT NULL,public_key TEXT NOT NULL,expelled INTEGER);
		CREATE UNIQUE INDEX members_active_key ON members(public_key) WHERE expelled IS NULL;
		CREATE TABLE epochs(revision INTEGER PRIMARY KEY,epoch TEXT UNIQUE NOT NULL,payload BLOB NOT NULL,signature BLOB NOT NULL);
		CREATE TABLE challenges(hash TEXT PRIMARY KEY,purpose TEXT NOT NULL,subject TEXT NOT NULL,expires INTEGER NOT NULL);
		CREATE TABLE pending(id TEXT PRIMARY KEY,name TEXT NOT NULL,public_key TEXT NOT NULL,
		 retrieval_hash TEXT NOT NULL,expires INTEGER NOT NULL,member_id TEXT REFERENCES members(id),retrieved INTEGER NOT NULL DEFAULT 0);
		CREATE TABLE credentials(id TEXT PRIMARY KEY,value BLOB NOT NULL);
		CREATE TABLE web_challenges(hash TEXT PRIMARY KEY,kind TEXT NOT NULL,value BLOB NOT NULL,expires INTEGER NOT NULL,authorization TEXT NOT NULL);
		CREATE TABLE sessions(hash TEXT PRIMARY KEY,verified INTEGER NOT NULL,expires INTEGER NOT NULL);
		CREATE TABLE rate_limits(bucket TEXT PRIMARY KEY,expires INTEGER NOT NULL,attempts INTEGER NOT NULL);
		CREATE TABLE audit(sequence INTEGER PRIMARY KEY AUTOINCREMENT,created INTEGER NOT NULL,
		 action TEXT NOT NULL,actor TEXT NOT NULL,target TEXT NOT NULL);
		PRAGMA user_version=1; COMMIT;`)
		if err != nil {
			db.Close()
			return nil, err
		}
	}
	if version < 2 {
		_, err = db.Exec(`BEGIN IMMEDIATE;
		CREATE TABLE recovery_events(generation TEXT PRIMARY KEY,challenge TEXT NOT NULL,
		 signature TEXT NOT NULL,authority_key TEXT NOT NULL,created INTEGER NOT NULL);
		PRAGMA user_version=2; COMMIT;`)
		if err != nil {
			db.Close()
			return nil, err
		}
	}
	if version < 3 {
		_, err = db.Exec(`BEGIN IMMEDIATE;
		CREATE TABLE conflict_decisions(vault TEXT NOT NULL,conflict TEXT NOT NULL,
		 version INTEGER NOT NULL,operation TEXT NOT NULL UNIQUE,payload BLOB NOT NULL,
		 PRIMARY KEY(vault,conflict,version));
		PRAGMA user_version=3; COMMIT;`)
		if err != nil {
			db.Close()
			return nil, err
		}
	}
	if version < 4 {
		_, err = db.Exec(`BEGIN IMMEDIATE;
		CREATE TABLE vault_secrets(generation TEXT NOT NULL,slot TEXT NOT NULL,
		 commitment TEXT NOT NULL,member TEXT NOT NULL,created INTEGER NOT NULL,
		 PRIMARY KEY(generation,slot));
		PRAGMA user_version=4; COMMIT;`)
		if err != nil {
			db.Close()
			return nil, err
		}
	}
	return db, nil
}

func Open(directory string) (*Store, error) {
	keyPath := filepath.Join(directory, "identity.key")
	info, err := os.Lstat(keyPath)
	if err != nil {
		return nil, err
	}
	if !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 {
		return nil, errors.New("authority identity must be a private regular file")
	}
	key, err := os.ReadFile(keyPath)
	if err != nil {
		return nil, err
	}
	if len(key) != ed25519.PrivateKeySize {
		return nil, errors.New("invalid authority identity")
	}
	db, err := openDatabase(directory)
	if err != nil {
		return nil, err
	}
	s := &Store{db: db, key: ed25519.PrivateKey(key), clock: time.Now}
	var expected, recovery string
	err = db.QueryRow("SELECT group_id,public_key,recovery_public FROM authority WHERE singleton=1").Scan(&s.Group, &expected, &recovery)
	if err == nil && expected != encoding.EncodeToString(s.key.Public().(ed25519.PublicKey)) {
		err = errors.New("authority key does not match control database")
	}
	if err == nil {
		s.RecoveryPublic, err = publicKey(recovery)
	}
	if err != nil {
		db.Close()
		return nil, err
	}
	return s, nil
}
func (s *Store) Close() error { return s.db.Close() }
func (s *Store) PublicKey() string {
	return encoding.EncodeToString(s.key.Public().(ed25519.PublicKey))
}

func (s *Store) publish(tx *sql.Tx, action, actor, target string) error {
	var rev int64
	var generation string
	if err := tx.QueryRow("UPDATE authority SET revision=revision+1 WHERE singleton=1 RETURNING revision,generation").Scan(&rev, &generation); err != nil {
		return err
	}
	state := State{Protocol: 1, Group: s.Group, Generation: generation, Revision: rev, Epoch: randomToken(), Members: []Member{}}
	rows, err := tx.Query("SELECT id,name,public_key FROM members WHERE expelled IS NULL ORDER BY id")
	if err != nil {
		return err
	}
	for rows.Next() {
		var m Member
		if err = rows.Scan(&m.ID, &m.Name, &m.PublicKey); err != nil {
			rows.Close()
			return err
		}
		state.Members = append(state.Members, m)
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return err
	}
	payload, err := json.Marshal(state)
	if err != nil {
		return err
	}
	signature := ed25519.Sign(s.key, append([]byte("ATHENA-HODARIUM-STATE-v1\x00"), payload...))
	if _, err = tx.Exec("INSERT INTO epochs VALUES(?,?,?,?)", rev, state.Epoch, payload, signature); err != nil {
		return err
	}
	_, err = tx.Exec("INSERT INTO audit(created,action,actor,target) VALUES(?,?,?,?)", s.clock().Unix(), action, actor, target)
	return err
}

func (s *Store) State() (SignedState, error) {
	var payload, signature []byte
	err := s.db.QueryRow("SELECT payload,signature FROM epochs ORDER BY revision DESC LIMIT 1").Scan(&payload, &signature)
	return SignedState{encoding.EncodeToString(payload), encoding.EncodeToString(signature)}, err
}

func ProofMessage(group, purpose, subject, challenge string) []byte {
	value, _ := json.Marshal([]string{"ATHENA-HODARIUM-PROOF-v1", group, purpose, subject, challenge})
	return value
}

// IssueChallenge must sit behind bounded HTTP admission and rate limiting.
func (s *Store) IssueChallenge(purpose, subject string) (string, error) {
	switch purpose {
	case "join", "resolve":
		if _, err := publicKey(subject); err != nil {
			return "", err
		}
	case "recover":
		if len(subject) != 87 || subject[43:] != "."+s.PublicKey() {
			return "", ErrDenied
		}
		if _, err := publicKey(subject[:43]); err != nil {
			return "", err
		}
	case "poll", "control", "rendezvous", "decision", "vault-secret":
		if len(subject) != 43 {
			return "", ErrDenied
		}
	default:
		return "", ErrDenied
	}
	tx, err := s.db.Begin()
	if err != nil {
		return "", err
	}
	defer tx.Rollback()
	if _, err = tx.Exec("DELETE FROM challenges WHERE expires<=?", s.clock().Unix()); err != nil {
		return "", err
	}
	var count int
	if err = tx.QueryRow("SELECT count(*) FROM challenges").Scan(&count); err != nil {
		return "", err
	}
	if count >= 4096 {
		return "", ErrCapacity
	}
	nonce := randomToken()
	_, err = tx.Exec("INSERT INTO challenges VALUES(?,?,?,?)", tokenHash(nonce), purpose, subject, s.clock().Add(time.Minute).Unix())
	if err != nil {
		return "", err
	}
	return nonce, tx.Commit()
}

func (s *Store) prove(purpose, subject, challenge, signature, key string) error {
	var expiry int64
	err := s.db.QueryRow("DELETE FROM challenges WHERE hash=? AND purpose=? AND subject=? RETURNING expires",
		tokenHash(challenge), purpose, subject).Scan(&expiry)
	if errors.Is(err, sql.ErrNoRows) {
		return ErrDenied
	}
	if err != nil {
		return err
	}
	pub, err := publicKey(key)
	if err != nil {
		return ErrDenied
	}
	sig, err := encoding.DecodeString(signature)
	if err != nil || expiry <= s.clock().Unix() || !ed25519.Verify(pub, ProofMessage(s.Group, purpose, subject, challenge), sig) {
		return ErrDenied
	}
	return nil
}

func (s *Store) Join(name, key, challenge, signature string) (Admission, error) {
	if len(name) == 0 || len(name) > 128 {
		return Admission{}, ErrDenied
	}
	if err := s.prove("join", key, challenge, signature, key); err != nil {
		return Admission{}, err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return Admission{}, err
	}
	defer tx.Rollback()
	if _, err = tx.Exec("DELETE FROM pending WHERE expires<=?", s.clock().Unix()); err != nil {
		return Admission{}, err
	}
	var count int
	if err = tx.QueryRow("SELECT count(*) FROM pending").Scan(&count); err != nil {
		return Admission{}, err
	}
	if count >= 256 {
		return Admission{}, ErrCapacity
	}
	p := Pending{ID: randomToken(), Name: name, PublicKey: key, Expires: s.clock().Add(10 * time.Minute).Unix()}
	credential := randomToken()
	_, err = tx.Exec("INSERT INTO pending(id,name,public_key,retrieval_hash,expires) VALUES(?,?,?,?,?)", p.ID, p.Name, p.PublicKey, tokenHash(credential), p.Expires)
	if err != nil {
		return Admission{}, err
	}
	if err = tx.Commit(); err != nil {
		return Admission{}, err
	}
	return Admission{p, credential, s.code(p.ID), (s.clock().Unix()/30 + 1) * 30}, nil
}

func (s *Store) code(id string) string {
	h := hmac.New(sha256.New, s.key.Seed())
	data, _ := json.Marshal([]any{"ATHENA-HODARIUM-ADMISSION-CODE-v1", s.Group, id, s.clock().Unix() / 30})
	h.Write(data)
	return fmt.Sprintf("%08d", binary.BigEndian.Uint64(h.Sum(nil)[:8])%100000000)
}

// LookupCode requires a recently verified admin session and a persistent attempt
// budget in the HTTP layer. Rotation never resets that session's budget.
func (s *Store) LookupCode(code string) (Pending, error) {
	if len(code) != 8 {
		return Pending{}, ErrDenied
	}
	rows, err := s.db.Query("SELECT id,name,public_key,expires FROM pending WHERE expires>? AND member_id IS NULL", s.clock().Unix())
	if err != nil {
		return Pending{}, err
	}
	defer rows.Close()
	var found Pending
	count := 0
	for rows.Next() {
		var p Pending
		if err = rows.Scan(&p.ID, &p.Name, &p.PublicKey, &p.Expires); err != nil {
			return Pending{}, err
		}
		if hmac.Equal([]byte(s.code(p.ID)), []byte(code)) {
			found = p
			count++
		}
	}
	if err = rows.Err(); err != nil {
		return Pending{}, err
	}
	if count != 1 {
		return Pending{}, ErrDenied
	}
	return found, nil
}

func (s *Store) Admit(id, approvedKey, actor string, expectedRevision int64) (string, error) {
	tx, err := s.db.Begin()
	if err != nil {
		return "", err
	}
	defer tx.Rollback()
	if err = s.requireAdmin(tx, actor); err != nil {
		return "", err
	}
	var name, key string
	err = tx.QueryRow("SELECT name,public_key FROM pending WHERE id=? AND member_id IS NULL AND expires>?", id, s.clock().Unix()).Scan(&name, &key)
	if errors.Is(err, sql.ErrNoRows) {
		return "", ErrDenied
	}
	if err != nil {
		return "", err
	}
	if key != approvedKey {
		return "", ErrDenied
	}
	var revision int64
	if err = tx.QueryRow("SELECT revision FROM authority").Scan(&revision); err != nil {
		return "", err
	}
	if revision != expectedRevision {
		return "", ErrConflict
	}
	var memberCount int
	if err = tx.QueryRow("SELECT count(*) FROM members WHERE expelled IS NULL").Scan(&memberCount); err != nil {
		return "", err
	}
	if memberCount >= 4096 {
		return "", ErrCapacity
	}
	var active int
	if err = tx.QueryRow("SELECT count(*) FROM members WHERE public_key=? AND expelled IS NULL", key).Scan(&active); err != nil {
		return "", err
	}
	if active != 0 {
		return "", ErrConflict
	}
	member := randomToken()
	if _, err = tx.Exec("INSERT INTO members(id,name,public_key) VALUES(?,?,?)", member, name, key); err != nil {
		return "", err
	}
	if _, err = tx.Exec("UPDATE pending SET member_id=? WHERE id=?", member, id); err != nil {
		return "", err
	}
	if err = s.publish(tx, "admit", "administrator", member); err != nil {
		return "", err
	}
	return member, tx.Commit()
}

func (s *Store) Expel(member, actor string, expectedRevision int64) error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if err = s.requireAdmin(tx, actor); err != nil {
		return err
	}
	var revision int64
	if err = tx.QueryRow("SELECT revision FROM authority").Scan(&revision); err != nil {
		return err
	}
	if revision != expectedRevision {
		return ErrConflict
	}
	result, err := tx.Exec("UPDATE members SET expelled=? WHERE id=? AND expelled IS NULL", s.clock().Unix(), member)
	if err != nil {
		return err
	}
	count, err := result.RowsAffected()
	if err != nil {
		return err
	}
	if count != 1 {
		return ErrDenied
	}
	if err = s.publish(tx, "expel", "administrator", member); err != nil {
		return err
	}
	return tx.Commit()
}

func (s *Store) Poll(id, credential, challenge, signature string) (Admission, error) {
	var p Pending
	var digest string
	err := s.db.QueryRow("SELECT id,name,public_key,expires,COALESCE(member_id,''),retrieval_hash FROM pending WHERE id=? AND retrieved=0", id).
		Scan(&p.ID, &p.Name, &p.PublicKey, &p.Expires, &p.MemberID, &digest)
	if errors.Is(err, sql.ErrNoRows) {
		return Admission{}, ErrDenied
	}
	if err != nil {
		return Admission{}, err
	}
	if !hmac.Equal([]byte(digest), []byte(tokenHash(credential))) || p.Expires <= s.clock().Unix() {
		return Admission{}, ErrDenied
	}
	if err = s.prove("poll", id, challenge, signature, p.PublicKey); err != nil {
		return Admission{}, err
	}
	if p.MemberID == "" {
		return Admission{Pending: p, Code: s.code(p.ID), CodeExpires: (s.clock().Unix()/30 + 1) * 30}, nil
	}
	// Conditional consumption also handles simultaneous retrieval attempts.
	result, err := s.db.Exec("UPDATE pending SET retrieved=1 WHERE id=? AND retrieved=0 AND EXISTS(SELECT 1 FROM members WHERE id=? AND expelled IS NULL)", id, p.MemberID)
	if err != nil {
		return Admission{}, err
	}
	count, err := result.RowsAffected()
	if err != nil {
		return Admission{}, err
	}
	if count != 1 {
		return Admission{}, ErrDenied
	}
	return Admission{Pending: p}, nil
}

type Validation struct {
	State             SignedState `json:"state"`
	Challenge         string      `json:"challenge"`
	Member            string      `json:"member"`
	MaxOfflineSeconds int         `json:"max_offline_seconds"`
	Signature         string      `json:"signature"`
}

// ResolveDevice recovers the result of an interrupted admission without
// reusing its one-shot retrieval credential or creating another member.
// It grants no offline lease; the device must still validate the returned ID.
func (s *Store) ResolveDevice(key, challenge, signature string) (Member, error) {
	if _, err := publicKey(key); err != nil {
		return Member{}, err
	}
	if err := s.prove("resolve", key, challenge, signature, key); err != nil {
		return Member{}, err
	}
	var member Member
	err := s.db.QueryRow("SELECT id,name,public_key FROM members WHERE public_key=? AND expelled IS NULL", key).
		Scan(&member.ID, &member.Name, &member.PublicKey)
	if errors.Is(err, sql.ErrNoRows) {
		return Member{}, ErrDenied
	}
	return member, err
}

func (s *Store) ValidateMember(ctx context.Context, member, challenge, signature string) (Validation, error) {
	var key string
	err := s.db.QueryRowContext(ctx, "SELECT public_key FROM members WHERE id=? AND expelled IS NULL", member).Scan(&key)
	if errors.Is(err, sql.ErrNoRows) {
		return Validation{}, ErrDenied
	}
	if err != nil {
		return Validation{}, err
	}
	if err = s.prove("control", member, challenge, signature, key); err != nil {
		return Validation{}, err
	}
	// Read membership and the exact publication in one snapshot, so expulsion
	// cannot grant fresh validity for a publication that excludes this device.
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Validation{}, err
	}
	defer tx.Rollback()
	var active int
	if err = tx.QueryRow("SELECT 1 FROM members WHERE id=? AND expelled IS NULL", member).Scan(&active); err != nil {
		return Validation{}, ErrDenied
	}
	var payload, sig []byte
	if err = tx.QueryRow("SELECT payload,signature FROM epochs ORDER BY revision DESC LIMIT 1").Scan(&payload, &sig); err != nil {
		return Validation{}, err
	}
	v := Validation{State: SignedState{encoding.EncodeToString(payload), encoding.EncodeToString(sig)}, Challenge: challenge, Member: member, MaxOfflineSeconds: 86400}
	message, _ := json.Marshal([]any{"ATHENA-HODARIUM-VALIDATION-v1", s.Group, v.State, member, challenge, v.MaxOfflineSeconds})
	v.Signature = encoding.EncodeToString(ed25519.Sign(s.key, message))
	return v, tx.Commit()
}
