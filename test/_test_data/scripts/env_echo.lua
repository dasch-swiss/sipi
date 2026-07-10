-- Echoes an env var read via `os.getenv`, proving the Rust binary's Lua VM
-- still sees process environment variables (no accidental env-scrubbing or
-- sandboxing). A route, not a preflight: routes get a real response sink, so
-- both the set and unset cases are safe to exercise here.

require "send_response"

local host = os.getenv("KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST")

if host then
    send_success({ host = host })
else
    send_error(500, "KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST is not set")
end
