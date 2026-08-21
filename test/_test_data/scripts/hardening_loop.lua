-- An infinite loop before any body byte: the deadline kill must surface as a
-- pre-commit 500. The pcall makes it a trapped kill — the re-arming hook must
-- still prevent any useful progress after the timeout fires.
pcall(function()
    while true do
    end
end)
-- Unreachable while the kill contract holds.
server.print("SURVIVED_THE_KILL")
