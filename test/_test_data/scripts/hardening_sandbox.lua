-- Probes the hardened VM profile from inside a script: whitelisted stdlib
-- present, escape hatches and dropped bindings absent, os reduced to the shim.
-- Prints "SANDBOX_OK" when every probe passes, else the first failure.

local failures = {}

local function must_be_nil(name, value)
    if value ~= nil then
        table.insert(failures, name .. " is reachable")
    end
end

local function must_be_function(name, value)
    if type(value) ~= "function" then
        table.insert(failures, name .. " is missing")
    end
end

local function must_be_table(name, value)
    if type(value) ~= "table" then
        table.insert(failures, name .. " is missing")
    end
end

-- Whitelisted stdlib present
must_be_table("string", string)
must_be_table("table", table)
must_be_table("math", math)
must_be_table("utf8", utf8)
must_be_table("package", package)

-- Never-loaded libraries
must_be_nil("io", io)
must_be_nil("debug", debug)

-- Scrubbed base-library escape hatches
must_be_nil("dofile", dofile)
must_be_nil("loadfile", loadfile)
must_be_nil("load", load)
must_be_nil("collectgarbage", collectgarbage)
must_be_nil("package.loadlib", package.loadlib)
must_be_nil("package.searchpath", package.searchpath)

-- The os shim: exactly getenv/clock/date
must_be_table("os", os)
must_be_function("os.getenv", os.getenv)
must_be_function("os.clock", os.clock)
must_be_function("os.date", os.date)
for _, name in ipairs({
    "execute",
    "popen",
    "exit",
    "remove",
    "rename",
    "tmpname",
    "time",
    "difftime",
    "setlocale",
}) do
    must_be_nil("os." .. name, os[name])
end
local year = os.date("%Y")
if type(year) ~= "string" or not year:match("^%d%d%d%d$") then
    table.insert(failures, "os.date('%Y') did not return a 4-digit year")
end

-- Dropped bindings
must_be_nil("server.shutdown", server.shutdown)
must_be_nil("server.fs.chdir", server.fs.chdir)
must_be_nil("config.password", config.password)
must_be_nil("config.adminuser", config.adminuser)

if #failures == 0 then
    server.print("SANDBOX_OK")
else
    server.print(table.concat(failures, "; "))
end
