// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
package main

import (
	"context"
	"crypto/tls"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"athena.local/hodarium/internal/authority"
	"athena.local/hodarium/web"
)

func run() error {
	if len(os.Args) < 2 {
		return errors.New("usage: athena-hodarium-server init|serve|recover [options]")
	}
	if os.Args[1] == "recover" {
		return recoverAuthority(os.Args[2:])
	}
	flags := flag.NewFlagSet(os.Args[1], flag.ContinueOnError)
	directory := flags.String("data", "", "private authority state directory")
	listen := flags.String("listen", "127.0.0.1:7443", "HTTPS listen address")
	origin := flags.String("origin", "", "stable HTTPS origin for Passkeys")
	cert := flags.String("tls-cert", "", "TLS certificate PEM")
	key := flags.String("tls-key", "", "TLS private key PEM")
	notifications := flags.String("notifications-config", "", "optional private JSON file for Resend security notifications")
	if err := flags.Parse(os.Args[2:]); err != nil {
		return err
	}
	if *directory == "" || flags.NArg() != 0 {
		return errors.New("--data is required; unexpected positional arguments are not accepted")
	}
	switch os.Args[1] {
	case "init":
		bootstrap, recovery, err := authority.Initialize(*directory)
		if err != nil {
			return err
		}
		fmt.Printf("Bootstrap credential (expires in 24 hours): %s\nOffline recovery seed (store securely, not on this server): %s\n", bootstrap, recovery)
		return nil
	case "serve":
		if *cert == "" || *key == "" {
			return errors.New("--tls-cert and --tls-key are required")
		}
		s, err := authority.Open(*directory)
		if err != nil {
			return err
		}
		defer s.Close()
		handler, err := authority.NewAPI(s, *origin)
		if err != nil {
			return err
		}
		mux := http.NewServeMux()
		mux.Handle("/api/", handler)
		mux.Handle("/", web.Handler())
		server := &http.Server{Addr: *listen, Handler: mux, ReadHeaderTimeout: 5 * time.Second,
			ReadTimeout: 15 * time.Second, WriteTimeout: 15 * time.Second, IdleTimeout: 60 * time.Second,
			MaxHeaderBytes: 16 * 1024, TLSConfig: &tls.Config{MinVersion: tls.VersionTLS13}}
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer stop()
		if *notifications != "" {
			stopNotifications, err := s.StartNotifications(ctx, *notifications, filepath.Join(*directory, "notifications.sqlite"))
			if err != nil {
				// Never log provider/config errors, which may contain credentials.
				slog.Warn("Hodarium notifications unavailable; check private configuration and queue; authentication remains available")
			} else {
				defer stopNotifications()
			}
		}
		done := make(chan error, 1)
		go func() { done <- server.ListenAndServeTLS(*cert, *key) }()
		slog.Info("Hodarium authority starting", "listen", *listen, "origin", *origin, "group", s.Group)
		select {
		case err := <-done:
			if errors.Is(err, http.ErrServerClosed) {
				return nil
			}
			return err
		case <-ctx.Done():
			shutdown, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()
			if err := server.Shutdown(shutdown); err != nil {
				server.Close()
				return err
			}
			return nil
		}
	default:
		return errors.New("unknown command; expected init, serve or recover")
	}
}
func main() {
	if err := run(); err != nil {
		slog.Error("Hodarium server", "error", err)
		os.Exit(1)
	}
}
