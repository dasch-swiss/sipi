-- Test script for server.http error handling.
-- Attempts to fetch an unreachable URL and returns the error gracefully.

require "send_response"

-- Use a definitely-unreachable address (RFC 5737 test range)
local success, result = server.http("GET", "http://192.0.2.1:1/unreachable", nil, 2000)

if not success then
    -- Expected: HTTP request failed, but Lua script handles it gracefully
    send_success({
        http_success = false,
        error_message = tostring(result)
    })
    return
end

-- Unexpected: if it somehow succeeded
send_success({
    http_success = true,
    status_code = result.status_code
})
