-- Probes the restricted require: a plain module name in the script dir loads;
-- anything with path separators or traversal is rejected.

local ok, m = pcall(require, "hardening_module")
if not ok or m.greet() ~= "MODULE_LOADED" then
    server.print("plain require failed: " .. tostring(m))
    return
end

for _, name in ipairs({
    "../scripts/hardening_module",
    "subdir/hardening_module",
    "/etc/passwd",
    "hardening_module.lua",
}) do
    local escaped, err = pcall(require, name)
    if escaped then
        server.print("require('" .. name .. "') should have been rejected")
        return
    end
    local _ = err
end

server.print("REQUIRE_OK")
