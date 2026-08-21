-- Pins the lowercase-header-keys invariant: every key in server.header is
-- lowercase, and a mixed-case request header is reachable under its lowercase
-- name.
for key, _ in pairs(server.header) do
    if key ~= key:lower() then
        server.print("non-lowercase header key: " .. key)
        return
    end
end

local probe = server.header["x-mixed-case"]
if probe == nil then
    server.print("x-mixed-case header not found")
    return
end

server.print("HEADERS_OK:" .. probe)
