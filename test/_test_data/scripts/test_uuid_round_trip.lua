-- Test script for UUID <-> Base62 round-trip.
-- Generates a UUID, converts to base62 and back, verifies identity.

require "send_response"

local success, uuid = server.uuid()
if not success then
    send_error(500, "uuid() failed: " .. tostring(uuid))
    return
end

local success, base62 = server.uuid_to_base62(uuid)
if not success then
    send_error(500, "uuid_to_base62 failed: " .. tostring(base62))
    return
end

local success, back = server.base62_to_uuid(base62)
if not success then
    send_error(500, "base62_to_uuid failed: " .. tostring(back))
    return
end

send_success({
    uuid = uuid,
    base62 = base62,
    back = back,
    round_trip_ok = (uuid == back)
})
