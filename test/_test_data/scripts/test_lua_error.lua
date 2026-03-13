-- Test script that deliberately triggers a Lua runtime error.
-- Used to verify sipi returns 500 (not crash) on Lua errors.
error("deliberate test error from Lua script")
