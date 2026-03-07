--
-- Local development configuration for SIPI
--
-- Usage:
--   ./build/sipi --config config/sipi.localdev-config.lua
--
-- Points imgroot at test data so IIIF requests work out of the box.
-- Small cache limits make it easy to observe eviction and metrics.
--
-- Example requests:
--   curl http://localhost:1024/unit/Leaves8.tif/full/max/0/default.jpg -o /tmp/test.jpg
--   curl http://localhost:1024/metrics
--   curl http://localhost:1024/api/cache
--
sipi = {
    hostname = 'localhost',
    port = 1024,
    nthreads = 4,
    keep_alive = 5,

    -- Use bundled test images so IIIF requests work immediately
    imgroot = './test/_test_data/images',
    prefix_as_path = true,
    subdir_levels = 0,

    -- Small cache to easily trigger eviction
    cache_dir = './cache',
    cache_size = '1M',
    cache_nfiles = 10,

    jpeg_quality = 80,
    scaling_quality = {
        jpeg = 'medium',
        tiff = 'high',
        png = 'high',
        j2k = 'high'
    },

    thumb_size = '!128,128',
    max_post_size = '50M',
    tmpdir = '/tmp',

    initscript = './config/sipi.init.lua',
    scriptdir = './scripts',

    ssl_port = 1025,
    ssl_certificate = './certificate/certificate.pem',
    ssl_key = './certificate/key.pem',

    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',

    loglevel = 'DEBUG'
}

admin = {
    user = 'admin',
    password = 'Sipi-Admin'
}

fileserver = {
    docroot = './server',
    wwwroute = '/server'
}

routes = {
    {
        method = 'DELETE',
        route = '/api/cache',
        script = 'cache.lua'
    },
    {
        method = 'GET',
        route = '/api/cache',
        script = 'cache.lua'
    },
    {
        method = 'GET',
        route = '/api/exit',
        script = 'exit.lua'
    }
}
