--
-- Configuration for the dsp-api production-closure e2e suite
-- (dsp_api_closure.rs): a copy of dsp-api's `sipi.docker-config.lua`, path
-- keys adapted to the fixture tree under `test/_test_data/dsp-api/`. The
-- `scripts/` directory carries dsp-api's live 9-file Lua closure verbatim.
-- The `delete_temp_file.lua` route is kept although the script does not
-- exist: dsp-api's production configs route it too, and a missing route
-- script must stay a request-time 404, never a boot failure.
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
    imgroot = './dsp-api/images',
    prefix_as_path = true,
    initscript = './dsp-api/scripts/sipi.init.lua',
    cache_dir = './cache',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './dsp-api/scripts',
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
        method = 'DELETE',
        route = '/delete_temp_file',
        script = 'delete_temp_file.lua'
    }
}
