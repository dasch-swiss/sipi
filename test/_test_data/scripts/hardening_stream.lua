-- Streams 64 x 256 KiB chunks (16 MiB). A client that stops reading stalls
-- the body channel; the write bound must fail the script at the deadline
-- instead of pinning the blocking thread until the client resumes.
local chunk = string.rep("x", 256 * 1024)
for _ = 1, 64 do
    server.print(chunk)
end
