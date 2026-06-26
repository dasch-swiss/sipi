-- Deterministic GET route for the TOML/Lua config-parity e2e test. Uses only the
-- built-in `server` table (no `require`), so it runs under an empty init script.
server.sendHeader("Content-Type", "text/plain")
server.sendStatus(200)
server.print("toml-route-ok")
