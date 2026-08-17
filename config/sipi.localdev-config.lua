--
-- Local development configuration for SIPI
--
-- Usage:
--   just nix-run                                   -- reproducible (consumes ./result/bin/sipi)
--   ./result/bin/sipi server --config config/sipi.localdev-config.lua
--   -- or, from the dev-shell inner loop:
--   ./build/sipi server --config config/sipi.localdev-config.lua
--
-- Points imgroot at test data so IIIF requests work out of the box.
-- Small cache limits make it easy to observe eviction and metrics.
--
-- Example requests:
--   curl http://localhost:1024/unit/Leaves8.tif/full/max/0/default.jpg -o /tmp/test.jpg
--   curl http://localhost:1024/metrics
--
sipi = {
    port = 1024,

    -- Use bundled test images so IIIF requests work immediately
    imgroot = './test/_test_data/images',
    prefix_as_path = true,

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

    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
}

admin = {
    user = 'admin',
    password = 'Sipi-Admin'
}

fileserver = {
    docroot = './server',
    wwwroute = '/server'
}

routes = {}
