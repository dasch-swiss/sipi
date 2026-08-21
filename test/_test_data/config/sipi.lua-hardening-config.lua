--
-- Configuration for the Lua-runtime hardening e2e suite (lua_hardening.rs):
-- routes exercising the sandbox, the restricted require, the deadline/memory
-- kills, the lowercase-header invariant, and a route whose script is missing
-- on disk (must stay a request-time 404). No initscript — the preflight
-- hardening tests write their own config + init pairs.
--
sipi = {
    port = 1024,
    jpeg_quality = 60,
    scaling_quality = {
        jpeg = "medium",
        tiff = "high",
        png = "high",
        j2k = "high"
    },
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    cache_dir = './cache',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    thumb_size = '!128,128',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = 'localhost',
    knora_port = '3434',
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
}

fileserver = {
    docroot = './server',
    wwwroute = '/server'
}

routes = {
    {
        method = 'GET',
        route = '/hardening/sandbox',
        script = 'hardening_sandbox.lua'
    },
    {
        method = 'GET',
        route = '/hardening/require',
        script = 'hardening_require.lua'
    },
    {
        method = 'GET',
        route = '/hardening/loop',
        script = 'hardening_loop.lua'
    },
    {
        method = 'GET',
        route = '/hardening/loop_committed',
        script = 'hardening_loop_committed.lua'
    },
    {
        method = 'GET',
        route = '/hardening/memory',
        script = 'hardening_memory.lua'
    },
    {
        method = 'GET',
        route = '/hardening/memory_trapped',
        script = 'hardening_memory_trapped.lua'
    },
    {
        method = 'GET',
        route = '/hardening/headers',
        script = 'hardening_headers.lua'
    },
    {
        method = 'GET',
        route = '/hardening/error_committed',
        script = 'hardening_error_committed.lua'
    },
    {
        method = 'GET',
        route = '/hardening/stream',
        script = 'hardening_stream.lua'
    },
    {
        method = 'GET',
        route = '/hardening/missing',
        script = 'hardening_missing_script.lua'
    }
}
