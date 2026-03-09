# Downstream Dependencies

This page documents which runtime packages the `daschswiss/sipi` Docker image provides and why, so downstream consumers don't need to rediscover this.

## Sipi's Bundled Lua Scripts

The Lua scripts shipped with Sipi (`sipi.config.lua`, `sipi.init.lua`, `test_functions.lua`, `send_response.lua`) have **no system tool dependencies**. They use only Sipi's built-in Lua API (`server.http()`, `server.decode_jwt()`, `server.parse_mimetype()`, etc.) — no `io.popen()` or `os.execute()` calls.

## Runtime Image Packages

The `daschswiss/sipi` Docker image (final stage) includes these packages:

| Package                 | Required by                       | How it's used                                                                                                                                                                                  |
| ----------------------- | --------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `curl`                  | `knora-sipi` healthcheck          | `healthcheck.sh`: `curl -sS --fail 'http://localhost:1024/...'`                                                                                                                                |
| `openssl`               | sipi binary                       | TLS for outbound HTTPS connections                                                                                                                                                             |
| `ca-certificates`       | sipi binary                       | TLS certificate trust store for HTTPS                                                                                                                                                          |
| `locales`               | sipi binary                       | UTF-8 locale (`en_US.UTF-8`, `sr_RS.UTF-8`) for string handling                                                                                                                                |
| `ffmpeg`                | dsp-ingest (`MovingImageService`) | `ffprobe` for video metadata (dimensions, duration, FPS). dsp-ingest runs `docker run --entrypoint ffprobe daschswiss/knora-sipi:...` in local dev, or calls `ffprobe` directly in production. |
| `libmagic1` + `file`    | sipi binary                       | MIME type detection (linked at compile time; runtime `.mgc` database needed)                                                                                                                   |
| `tzdata`                | system                            | Timezone support (`TZ=Europe/Zurich`)                                                                                                                                                          |
| `sha256sum` (coreutils) | `knora-sipi` Lua scripts          | `util.lua:file_checksum()` calls `/usr/bin/sha256sum`                                                                                                                                          |

### Packages Removed

The following packages were previously included but had no downstream consumer: `imagemagick`, `at`, `bc`, `uuid`, `byobu`, `htop`, `man`, `vim`, `git`, `unzip`, `wget`, `gnupg2`, `software-properties-common`.

## Downstream Consumers

| Consumer                                  | Image                                                                        | What it uses from sipi container                                                                                 |
| ----------------------------------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `knora-sipi` (dsp-api `sipi/` subproject) | `daschswiss/knora-sipi` (base: `daschswiss/sipi`)                            | Lua scripts + sipi HTTP server. Needs `curl`, `sha256sum`, `libmagic1`, `locales`, `openssl`, `ca-certificates`. |
| dsp-ingest (`SipiClientLive`)             | `daschswiss/knora-sipi` (via `docker run` in local dev)                      | sipi CLI (`--query`, `--format`, `--topleft`). Needs the sipi binary.                                            |
| dsp-ingest (`MovingImageService`)         | `daschswiss/knora-sipi` (via `docker run --entrypoint ffprobe` in local dev) | `ffprobe` for video metadata extraction. Needs `ffmpeg` package.                                                 |
| dsp-tools                                 | `daschswiss/knora-sipi` (via Docker Compose)                                 | HTTP API only (port 1024). No direct tool dependencies on container internals.                                   |
| fileidentification                        | none                                                                         | No dependency on sipi. Standalone tool with its own ffmpeg/imagemagick/libreoffice.                              |
