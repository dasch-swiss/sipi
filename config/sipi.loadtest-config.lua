-- Config for the concurrent-load decode harness (`just loadtest-decode`).
--
-- Deliberately differs from the localdev config for a decode-throughput
-- measurement: a tiny cache (4 files) so that, combined with the harness
-- requesting a distinct tile region per request, every request misses the cache
-- and exercises a real JPEG2000 decode rather than a cache hit.
--
-- Pool sizing and the wait-queue are CLI/env knobs, not config-file keys. The
-- harness leaves SIPI_NTHREADS unset so the pool sizes from the host core count
-- (the production default = the pool's real saturation point); tune load-shedding
-- with SIPI_MAX_WAITING / SIPI_QUEUE_TIMEOUT if needed.
sipi = {
    hostname = 'localhost',
    port = 2048,
    keep_alive = 5,
    imgroot = './test/_test_data/images',
    prefix_as_path = true,
    subdir_levels = 0,
    cache_dir = './loadtest_cache',
    cache_size = '1M',
    cache_nfiles = 4,              -- tiny; distinct tile regions guarantee decode-every-request anyway
    jpeg_quality = 80,
    scaling_quality = { jpeg = 'medium', tiff = 'high', png = 'high', j2k = 'high' },
    thumb_size = '!128,128',
    max_post_size = '50M',
    tmpdir = '/tmp',
    initscript = './config/sipi.init.lua',
    scriptdir = './scripts',
    ssl_port = 2049,
    ssl_certificate = './certificate/certificate.pem',
    ssl_key = './certificate/key.pem',
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
    loglevel = 'WARN'
}
admin = { user = 'admin', password = 'Sipi-Admin' }
fileserver = { docroot = './server', wwwroute = '/server' }
routes = {}
