--
-- Configuration for the Lua-route admission-lane e2e (admission_control.rs):
-- a single slow route that holds its permit ~2s, so the test can prove a Lua
-- route occupies the full partition (second script sheds while a tile still
-- admits). The pool is sized by the test via SIPI_NTHREADS/SIPI_MAX_WAITING.
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

routes = {
    {
        method = 'GET',
        route = '/lane/slow',
        script = 'lane_slow.lua'
    }
}
