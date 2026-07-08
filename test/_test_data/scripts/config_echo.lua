-- Echoes a handful of `sipiConfGlobals`-installed config values as JSON.
-- Used by the plan 02 §7.7 flag matrix to observe --thumbsize/--knorapath/
-- --knoraport CLI/env overrides, which have no other HTTP-visible effect.

require "send_response"

send_success({
    thumb_size = config.thumb_size,
    knora_path = config.knora_path,
    knora_port = config.knora_port
})
