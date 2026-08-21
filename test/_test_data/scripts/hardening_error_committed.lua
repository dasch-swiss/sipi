-- Commits the response head (first body byte streams it), then raises an
-- ordinary uncaught Lua error: the stream must be aborted — never a clean
-- EOF that reads as a complete 200 body.
server.print("partial")
error("boom after commit")
