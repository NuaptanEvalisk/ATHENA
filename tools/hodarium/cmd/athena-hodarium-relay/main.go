// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package main

import (
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"athena.local/hodarium/internal/relay"
)

func main() {
	listen := flag.String("listen", "127.0.0.1:9444", "TLS listen address")
	cert := flag.String("tls-cert", "", "TLS certificate PEM")
	key := flag.String("tls-key", "", "TLS private key PEM")
	credential := flag.String("access-token-file", "", "private file containing a 32-byte base64url relay access token")
	capacity := flag.Int("max-sessions", 256, "maximum reserved or active sessions")
	bytes := flag.Int64("max-bytes", 1<<30, "byte budget per direction per session")
	flag.Parse()
	if *cert == "" || *key == "" || *credential == "" {
		log.Fatal("TLS certificate, TLS key and access-token-file are required")
	}
	f, err := os.Open(*credential)
	if err != nil {
		log.Fatal(err)
	}
	info, err := f.Stat()
	if err != nil || !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 || info.Size() > 128 {
		f.Close()
		log.Fatal("relay credential must be a small private regular file")
	}
	data := make([]byte, info.Size())
	_, err = f.ReadAt(data, 0)
	f.Close()
	if err != nil {
		log.Fatal(err)
	}
	r, err := relay.New(relay.Config{AccessToken: strings.TrimSpace(string(data)), MaxSessions: *capacity, MaxBytes: *bytes,
		TicketLifetime: 2 * time.Minute, SessionLifetime: time.Hour, IdleTimeout: 90 * time.Second})
	clear(data)
	if err != nil {
		log.Fatal(err)
	}
	defer r.Close()
	s := &http.Server{Addr: *listen, Handler: r, ReadHeaderTimeout: 10 * time.Second, ReadTimeout: 15 * time.Second,
		WriteTimeout: 15 * time.Second, IdleTimeout: 30 * time.Second, MaxHeaderBytes: 8192,
		TLSConfig: &tls.Config{MinVersion: tls.VersionTLS13}}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		r.Close()
		shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = s.Shutdown(shutdown)
	}()
	fmt.Println("ATHENA Hodarium Relay listening on", *listen)
	if err = s.ListenAndServeTLS(*cert, *key); err != nil && err != http.ErrServerClosed {
		log.Fatal(err)
	}
}
