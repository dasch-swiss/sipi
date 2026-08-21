-- A memory bomb: doubling a string until the Lua allocator cap fires. The
-- untrapped allocation error must surface as a pre-commit 500 (a memory
-- kill), not an engine OOM.
local s = "x"
while true do
    s = s .. s
end
