-- A pcall-trapped memory bomb: trapping the allocation error is stock Lua
-- semantics (LUA_ERRMEM is catchable), but it must not lift the cap — a
-- follow-up over-cap allocation must fail exactly the same way.
local bombed = pcall(function()
    local s = "x"
    while true do
        s = s .. s
    end
end)
if bombed then
    server.print("the bomb never hit the cap")
    return
end

local again = pcall(function()
    local s = "y"
    while true do
        s = s .. s
    end
end)
if again then
    server.print("the cap was lifted after a trapped memory error")
    return
end

server.print("CAP_HELD")
