-- Holds its admission permit for ~2s of CPU so the lane e2e can probe the
-- pool while this request is in flight (admission_control.rs).
local t0 = os.clock()
while os.clock() - t0 < 2.0 do end
server.sendStatus(200)
server.print("SLOW_DONE")
