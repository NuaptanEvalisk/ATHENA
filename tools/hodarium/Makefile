.PHONY: server panel test clean

server: panel
	go build -p 20 -trimpath -o bin/athena-hodarium-server ./cmd/athena-hodarium-server

panel:
	npm ci --prefix web
	npm run build --prefix web

test:
	go test ./internal/authority

clean:
	rm -rf bin web/dist
