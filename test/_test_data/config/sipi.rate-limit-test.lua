--
-- Test config with rate limiting in enforce mode.
-- Very low budget (500k pixels, 60s window) so a single 512x512 request
-- (262144 pixels) nearly exhausts the budget and a second request triggers 429.
--
sipi = {
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = { jpeg = "medium", tiff = "high", png = "high", j2k = "high" },
    keep_alive = 5,
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    subdir_excludes = { "tmp", "thumb" },
    initscript = './config/sipi.init-knora.lua',
    cache_dir = './cache',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    thumb_size = '!128,128',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = 'localhost',
    knora_port = '3434',
    ssl_port = 1025,
    ssl_certificate = './certificate/certificate.pem',
    ssl_key = './certificate/key.pem',
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
    logfile = "sipi.log",
    loglevel = "DEBUG",

    -- Rate limiter: enforce mode, 500k pixel budget, 60s window
    rate_limit_mode = "enforce",
    rate_limit_max_pixels = 500000,
    rate_limit_window = 60,
    rate_limit_pixel_threshold = 1000
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
