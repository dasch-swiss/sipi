-- Lua half of the TOML/Lua config-parity e2e fixture. The matching TOML file is
-- sipi.toml-parity-test.toml; the `toml_config` e2e test starts a server with
-- each and asserts identical served behaviour. Cache is disabled so the two
-- configs are behaviourally equivalent (the Lua ctor otherwise defaults to a
-- 200M cache at ./cache, which the TOML side would not have).
sipi = {
    imgroot = './images',
    prefix_as_path = true,
    initscript = './scripts/parity.init.lua',
    scriptdir = './scripts',
    cache_size = '0',
    jpeg_quality = 60,
    thumb_size = '!128,128',
    loglevel = 'INFO',
}

routes = {
    {
        method = 'GET',
        route = '/parity',
        script = 'parity_route.lua'
    }
}
