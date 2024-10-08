# Changelog

## [3.14.0](https://github.com/dasch-swiss/sipi/compare/v3.13.0...v3.14.0) (2024-10-08)


### Features

* Respect X-Forwarded-Proto (DEV-3499) ([#453](https://github.com/dasch-swiss/sipi/issues/453)) ([d5eaab3](https://github.com/dasch-swiss/sipi/commit/d5eaab3bb36114759861c98a419ecca6955d41ce))
* Support IIIF Image's API HEAD requests (DEV-4072) ([#456](https://github.com/dasch-swiss/sipi/issues/456)) ([f3a9a96](https://github.com/dasch-swiss/sipi/commit/f3a9a9695a3c931e0d95144a07d1f7ba0bc182ae))
* Watermarks with alpha support and respect to ratio (DEV-4072) ([#458](https://github.com/dasch-swiss/sipi/issues/458)) ([da34f6a](https://github.com/dasch-swiss/sipi/commit/da34f6af0c51ceb3a47607e6f9ce9e2541a7497d))


### Bug Fixes

* Fix planar BigTIFF ingestion (DEV-3384) ([#455](https://github.com/dasch-swiss/sipi/issues/455)) ([83c2c94](https://github.com/dasch-swiss/sipi/commit/83c2c949fc9dc0c97fbd777f77a11adf38eac883))

## [3.13.0](https://github.com/dasch-swiss/sipi/compare/v3.12.3...v3.13.0) (2024-08-07)


### Features

* Allow YCbCr/RGB autoconvert, update libtiff (DEV-3863) ([#451](https://github.com/dasch-swiss/sipi/issues/451)) ([ccc68bc](https://github.com/dasch-swiss/sipi/commit/ccc68bc797e59befac48e29cab15b638a0b1f8aa))

## [3.12.3](https://github.com/dasch-swiss/sipi/compare/v3.12.2...v3.12.3) (2024-07-23)


### Bug Fixes

* Update ijg jpeg lib to v9f, partially fixes DEV-3474 ([#449](https://github.com/dasch-swiss/sipi/issues/449)) ([94e72fa](https://github.com/dasch-swiss/sipi/commit/94e72fa1cd3af41705e5f505e50bdaa28cee8b6b))

## [3.12.2](https://github.com/dasch-swiss/sipi/compare/v3.12.1...v3.12.2) (2024-04-07)


### Bug Fixes

* Triggering release creation ([8214a37](https://github.com/dasch-swiss/sipi/commit/8214a37fae0290eea6b29ff24760d288b0cb0baa))

## [3.12.1](https://github.com/dasch-swiss/sipi/compare/v3.12.0...v3.12.1) (2024-04-07)


### Bug Fixes

* Triggering release creation ([7434ed4](https://github.com/dasch-swiss/sipi/commit/7434ed42edb3869d542c96add443ae9af1d16b6d))

## [3.12.0](https://github.com/dasch-swiss/sipi/compare/v3.11.0...v3.12.0) (2024-04-07)


### Features

* Trying out release-please ([4ca8169](https://github.com/dasch-swiss/sipi/commit/4ca8169d87eee061350fed530e1318a2e21ed7f9))

## 3.11.0 (2024-04-05)


### Features

* -f option in curl, does not give output if status is not 200 ([d523213](https://github.com/dasch-swiss/sipi/commit/d523213f0eb657526f480dfe15bca6745709e716))
* Add better support for additional mimetypes, especially *.odd and *.rng ([#384](https://github.com/dasch-swiss/sipi/issues/384)) ([473474f](https://github.com/dasch-swiss/sipi/commit/473474f3d5f73f0f7fde99d2f5a9cce24f84d885))
* Add IIIF 3.0 support ([#324](https://github.com/dasch-swiss/sipi/issues/324)) ([d04725a](https://github.com/dasch-swiss/sipi/commit/d04725a55893e1326595a0560f6811c7923166e5))
* Add mimetype to knora json response ([#305](https://github.com/dasch-swiss/sipi/issues/305)) ([81ab98a](https://github.com/dasch-swiss/sipi/commit/81ab98a91ba7abc91fc33f4226034c2d1b9724aa))
* Add triggering of preflight script for non-image file types (DEV-1664) ([#381](https://github.com/dasch-swiss/sipi/issues/381)) ([b86428a](https://github.com/dasch-swiss/sipi/commit/b86428a952e7db85604da6a65e278a45d0bdddf4))
* Bash test script exit value ([876c270](https://github.com/dasch-swiss/sipi/commit/876c270c7485b0762fc0094d62664df1f7d289b5))
* Extend knora info for video ([#371](https://github.com/dasch-swiss/sipi/issues/371)) ([61cf681](https://github.com/dasch-swiss/sipi/commit/61cf68175e612b878f4cad92cfddec7df1acbc8a))
* Fixes, PDF, IIIF Auth, JPX compression parameters ([#290](https://github.com/dasch-swiss/sipi/issues/290)) ([6f46892](https://github.com/dasch-swiss/sipi/commit/6f46892fc36290cb1bd823b7b1b1659d4301fc79))
* Revert transparent JPG behavior ([#418](https://github.com/dasch-swiss/sipi/issues/418)) ([3675997](https://github.com/dasch-swiss/sipi/commit/3675997f9dc2249f48b9d7c54cef9ea162f697a6))
* Tilted images are imported without tilting (DEV-31) ([#374](https://github.com/dasch-swiss/sipi/issues/374)) ([8f65c6c](https://github.com/dasch-swiss/sipi/commit/8f65c6c451ddee5827bc209ae9399e12110294d7))
* Use gray instead of black values for transparent parts of image when returning as JPEG ([#412](https://github.com/dasch-swiss/sipi/issues/412)) ([f18dcfc](https://github.com/dasch-swiss/sipi/commit/f18dcfc28af5ce1f94a3762cc81cddda209a1461))


### Bug Fixes

* (SipiCache and Logger): added missing libraries ([6cb8a59](https://github.com/dasch-swiss/sipi/commit/6cb8a5955aa64715edf958b72dc02e24aebc660c))
* Add fix for [#83](https://github.com/dasch-swiss/sipi/issues/83). ([44b2090](https://github.com/dasch-swiss/sipi/commit/44b2090b8b60d3629adc91a1d041b85f612b8507))
* Add missing file ([#361](https://github.com/dasch-swiss/sipi/issues/361)) ([71ec889](https://github.com/dasch-swiss/sipi/commit/71ec88983c2eb97623f35e430646a6a48beb5388))
* Add missing files to Docker image ([#342](https://github.com/dasch-swiss/sipi/issues/342)) ([9960014](https://github.com/dasch-swiss/sipi/commit/996001478294909cd03241e34f293238e3beeaa6))
* Add missing include. ([e784251](https://github.com/dasch-swiss/sipi/commit/e784251452fffd9053c9034193a63534811c7c35))
* Better handling of missing sidecar files ([#376](https://github.com/dasch-swiss/sipi/issues/376)) ([bed711f](https://github.com/dasch-swiss/sipi/commit/bed711fc9aa41fea095f139c301f9dfd8f5185fc))
* Correct typo in favicon route. ([7a2e320](https://github.com/dasch-swiss/sipi/commit/7a2e3200de16da832f24d969536b19ed9a42d5c9))
* Crashing on jp2 decompression ([#407](https://github.com/dasch-swiss/sipi/issues/407)) ([93308c2](https://github.com/dasch-swiss/sipi/commit/93308c2bdd2d68cdea02808eda788833d2aaf83d))
* Docs deployment ([e884677](https://github.com/dasch-swiss/sipi/commit/e884677db6cec16b34e2b923b042d2031421b35a))
* Don’t make detached threads, and improve signal handling ([#93](https://github.com/dasch-swiss/sipi/issues/93)). ([4365500](https://github.com/dasch-swiss/sipi/commit/4365500644cc1cceb816d04d9711007e60364beb))
* Fix deadlock on mutex protecting thread_ids. ([f42d3e3](https://github.com/dasch-swiss/sipi/commit/f42d3e3620df18ecd5a65830f0a295736d188d89))
* Fix memory leak in SipiImage. ([aadc63e](https://github.com/dasch-swiss/sipi/commit/aadc63eaa9b89898b1ae85a421087792bcce610a))
* IIIF URL redirection ([#417](https://github.com/dasch-swiss/sipi/issues/417)) ([1905bf2](https://github.com/dasch-swiss/sipi/commit/1905bf2fe740960cc87dae23f71c8268ccde8fd9))
* Incorrect error message ([#383](https://github.com/dasch-swiss/sipi/issues/383)) ([94b50f9](https://github.com/dasch-swiss/sipi/commit/94b50f9985ab6582cfc234e229f37f1344d49c67))
* Invalid watermark crashes sipi ([#406](https://github.com/dasch-swiss/sipi/issues/406)) ([39dc0e8](https://github.com/dasch-swiss/sipi/commit/39dc0e87b5fcdebee162cf251736e3ee9a5229af))
* Issues ([#328](https://github.com/dasch-swiss/sipi/issues/328)) ([2d78c55](https://github.com/dasch-swiss/sipi/commit/2d78c553cfb7465ebdca0abf195f6cebdf1870a9))
* Kakadu error ([#341](https://github.com/dasch-swiss/sipi/issues/341))(DSP-1247) ([d0bc37a](https://github.com/dasch-swiss/sipi/commit/d0bc37a047a8ee75160d490ac9f6903470547c2e))
* Knora upload scripts and configs ([#174](https://github.com/dasch-swiss/sipi/issues/174)) ([51054d0](https://github.com/dasch-swiss/sipi/commit/51054d02f278320d5694b3063070b5dc1370e3c1))
* **knora.json:** Return origin instead of wildcard (DEV-318) ([#369](https://github.com/dasch-swiss/sipi/issues/369)) ([3b73ff7](https://github.com/dasch-swiss/sipi/commit/3b73ff726e987a675a6927c63b6b23e3c75584ea))
* Make signal handler code asynchronous-safe. ([8cc43b3](https://github.com/dasch-swiss/sipi/commit/8cc43b39b5b6177119834c655c8db3d6b5fffe66))
* Memory leaks (DEV-237) ([#365](https://github.com/dasch-swiss/sipi/issues/365)) ([c3b9b35](https://github.com/dasch-swiss/sipi/commit/c3b9b3519bfb2b3d10f50c72c2e81a8d2b506f35))
* Palette color tiffs now read correctly ([83037c5](https://github.com/dasch-swiss/sipi/commit/83037c581acfbcfd3552c7fb9f7d3be20986c427))
* Palette color tiffs now read correctly ([#253](https://github.com/dasch-swiss/sipi/issues/253)) ([83037c5](https://github.com/dasch-swiss/sipi/commit/83037c581acfbcfd3552c7fb9f7d3be20986c427))
* Parse url crash ([#340](https://github.com/dasch-swiss/sipi/issues/340)) (DSP-1247) ([e710237](https://github.com/dasch-swiss/sipi/commit/e7102379998ab38c0b6162fa4c4a3599eb328cb2))
* Printed version strings (DSP-687) ([#332](https://github.com/dasch-swiss/sipi/issues/332)) ([9778b19](https://github.com/dasch-swiss/sipi/commit/9778b19aa27536dd4b11574d6d62fb1824d021cf))
* Replace custom HTTP client code with libcurl. ([fb1bdfc](https://github.com/dasch-swiss/sipi/commit/fb1bdfced897b7e1e5268b8c2f9fe2859b83ae14))
* Return Internal Server Error if Lua function has invalid return value. ([2c72dde](https://github.com/dasch-swiss/sipi/commit/2c72dde59a847b43465f347d3d93aab4c238b5c2))
* Small bugfix in CMakeLists.txt ([cf1c275](https://github.com/dasch-swiss/sipi/commit/cf1c2758c385538f6a6f331780b6c0045a7aa24d))
* Support for grayscale jpegs ([#410](https://github.com/dasch-swiss/sipi/issues/410)) ([fd63dd0](https://github.com/dasch-swiss/sipi/commit/fd63dd089db12c7344d2665dff63b6d2586ae1de))
* Support for TIFF with CMYK and alpha channel ([#408](https://github.com/dasch-swiss/sipi/issues/408)) ([c0cc033](https://github.com/dasch-swiss/sipi/commit/c0cc033cab0cb8d86a267f532eff9e13eff6fee8))
* Take watermark into account when caching ([#421](https://github.com/dasch-swiss/sipi/issues/421)) ([18788b2](https://github.com/dasch-swiss/sipi/commit/18788b20e19d0fe7af97f6e791822f5d503d2650))
* Uploading PNGs with transparency crashes SIPI ([#375](https://github.com/dasch-swiss/sipi/issues/375)) ([01104b5](https://github.com/dasch-swiss/sipi/commit/01104b520ea5e360154bec5b2f4b8080a264e3e4))
* Use PATCH_COMMAND instead of UPDATE_COMMAND so the same patch isn't applied twice ([#53](https://github.com/dasch-swiss/sipi/issues/53)) ([1f9695a](https://github.com/dasch-swiss/sipi/commit/1f9695a8a0ee7b21090699e8ea3750694fa09d50))
* Use prefix if provided in sipi.init-knora-test.lua. ([4b99130](https://github.com/dasch-swiss/sipi/commit/4b99130f3d585e1928a12e85d6fe699c653608aa))
* Use RAII to manage libcurl connections. ([87f1366](https://github.com/dasch-swiss/sipi/commit/87f13664098fb18a22ce05c1d10416efe5848930))
* Watermark support ([#405](https://github.com/dasch-swiss/sipi/issues/405)) ([b7abe85](https://github.com/dasch-swiss/sipi/commit/b7abe857cc7acc6a9820ed327c5e913389d72a44))


### Miscellaneous Chores

* Release 3.11.0 ([f76eebb](https://github.com/dasch-swiss/sipi/commit/f76eebb7a91bd4722f3be5419c9f897336247abb))
