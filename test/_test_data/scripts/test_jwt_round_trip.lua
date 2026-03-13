-- Test script for JWT generate/decode round-trip.
-- Generates a JWT with a test payload, decodes it back, and verifies preservation.

require "send_response"

local payload = {
    iss = "sipi-test",
    sub = "test-user",
    custom_field = "hello-world",
    number_field = 42
}

local success, token = server.generate_jwt(payload)
if not success then
    send_error(500, "generate_jwt failed: " .. tostring(token))
    return
end

local success, decoded = server.decode_jwt(token)
if not success then
    send_error(500, "decode_jwt failed: " .. tostring(decoded))
    return
end

send_success({
    token = token,
    decoded = decoded,
    round_trip_ok = (decoded.iss == payload.iss and
                     decoded.sub == payload.sub and
                     decoded.custom_field == payload.custom_field)
})
