-- Test script for Lua state thread isolation.
-- Sets a global variable to the request's unique value and reads it back.
-- If Lua states leak between threads, the value may be overwritten.

require "send_response"

local request_id = server.get['id']
if not request_id then
    send_error(400, "Missing 'id' query parameter")
    return
end

-- Set a global variable in this Lua state
_G.test_isolation_value = request_id

-- Small delay to increase chance of detecting race conditions
-- (other concurrent requests may try to overwrite this global)
local start = os.clock()
while os.clock() - start < 0.01 do end

-- Read it back — should still be our request_id
local read_back = _G.test_isolation_value

send_success({
    request_id = request_id,
    read_back = read_back,
    isolated = (request_id == read_back)
})
