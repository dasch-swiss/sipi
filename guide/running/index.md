# Running SIPI

SIPI can be run either as a command-line image converter or as an IIIF media server.

## Quick Start with Docker

```
docker run -p 1024:1024 daschswiss/sipi
```

## Running SIPI as a Command-line Image Converter

Convert an image file to another format:

```
sipi --format jpg -f input.tif output.jpg
```

Query image file information:

```
sipi --query input.tif
```

Compare two image files pixel-wise:

```
sipi --compare file1.tif file2.jpg
```

## Running SIPI as a Server

```
sipi --config config/sipi.config.lua
```

## Logging

SIPI uses two logging modes depending on how it is running:

- **CLI mode** (`--file`, `--compare`, `--query`): Plain text output. Errors go to **stderr**, informational messages go to **stdout**. This is the standard Unix convention for command-line tools.
- **Server mode** (`--config`): JSON-formatted log lines go to **stdout**. This follows container best practices — Docker, Kubernetes, and log collectors (Grafana Loki, Fluentd) expect structured logs on stdout. Each line is a JSON object: `{"level": "INFO", "message": "..."}`.

### Log Levels

SIPI supports the following log levels (in order of increasing severity):

| Level     | Description                                                                               |
| --------- | ----------------------------------------------------------------------------------------- |
| `DEBUG`   | Detailed diagnostic information.                                                          |
| `INFO`    | Normal operational messages (routes added, server started, migrations).                   |
| `NOTICE`  | Significant but normal events.                                                            |
| `WARNING` | Something unexpected but recoverable (e.g., failed XMP parse, incomplete metadata write). |
| `ERR`     | Errors that affect a specific operation (e.g., image processing failure, ICC error).      |
| `CRIT`    | Critical errors.                                                                          |
| `ALERT`   | Conditions requiring immediate attention.                                                 |
| `EMERG`   | System-wide emergencies.                                                                  |

The log level controls which messages are emitted. Setting a level suppresses all messages below it. For example, `WARNING` shows only WARNING, ERR, CRIT, ALERT, and EMERG — suppressing DEBUG, INFO, and NOTICE.

The log level can be configured in three ways (in order of precedence):

1. **CLI option**: `--loglevel WARNING`
1. **Environment variable**: `SIPI_LOGLEVEL=WARNING`
1. **Lua config**: `loglevel = "WARNING"` (in the `sipi` block)

If none is specified, the default level is `INFO`.

## Command-line Options

### Image Conversion Options

| Flag                 | Short | Description                                                            |
| -------------------- | ----- | ---------------------------------------------------------------------- |
| `--file <path>`      | `-f`  | Input file to be converted. Usage: `sipi [options] -f infile outfile`  |
| `--format <fmt>`     | `-F`  | Output format: `jpx`, `jp2`, `jpg`, `tif`, `png`, `webp`, `gif`        |
| `--icc <profile>`    | `-I`  | Convert to ICC profile: `none`, `sRGB`, `AdobeRGB`, `GRAY`             |
| `--quality <1-100>`  | `-q`  | JPEG compression quality (1 = highest compression, 100 = best quality) |
| `--pagenum <n>`      | `-n`  | Page number for multi-page PDF/TIFF input files                        |
| `--region <x,y,w,h>` | `-r`  | Select a region of interest (4 integer values)                         |
| `--reduce <factor>`  | `-R`  | Reduce image size by factor (faster than `--scale`)                    |
| `--size <w,h>`       | `-s`  | Resize image to given dimensions                                       |
| `--scale <percent>`  | `-S`  | Resize image by percentage                                             |
| `--mirror <dir>`     | `-m`  | Mirror image: `none`, `horizontal`, `vertical`                         |
| `--rotate <angle>`   | `-o`  | Rotate image by degrees (0.0 - 360.0)                                  |
| `--skipmeta`         | `-k`  | Strip all metadata from the output file                                |
| `--topleft`          |       | Enforce TOPLEFT orientation                                            |
| `--watermark <file>` | `-w`  | Overlay a watermark (single-channel grayscale TIFF)                    |
| `--Ctiff_pyramid`    |       | Store output in pyramidal TIFF format                                  |

### Query and Compare

| Flag                  | Short | Description                               |
| --------------------- | ----- | ----------------------------------------- |
| `--query`             | `-x`  | Dump all information about the given file |
| `--compare <f1> <f2>` | `-C`  | Compare two files pixel-wise              |

### JPEG2000 Options

| Flag                    | Description                                                                                                                              |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `--Sprofile <val>`      | J2K profile: `PROFILE0`, `PROFILE1`, `PROFILE2`, `PART2`, `CINEMA2K`, `CINEMA4K`, `BROADCAST`, `CINEMA2S`, `CINEMA4S`, `CINEMASS`, `IMF` |
| `--rates <string>`      | Bit-rate(s) for quality layers (`-1` for lossless final layer)                                                                           |
| `--Clayers <n>`         | Number of quality layers (default: 8)                                                                                                    |
| `--Clevels <n>`         | Number of wavelet decomposition levels (default: 8)                                                                                      |
| `--Corder <val>`        | Progression order: `LRCP`, `RLCP`, `RPCL`, `PCRL`, `CPRL` (default: `RPCL`)                                                              |
| `--Stiles <string>`     | Tile dimensions `"{tx,ty}"` (default: `"{256,256}"`)                                                                                     |
| `--Cprecincts <string>` | Precinct dimensions `"{px,py}"` (default: `"{256,256}"`)                                                                                 |
| `--Cblk <string>`       | Code-block dimensions `"{dx,dy}"` (default: `"{64,64}"`)                                                                                 |
| `--Cuse_sop <val>`      | Include SOP markers (default: yes)                                                                                                       |

### Server Options

| Flag                    | Short | Env Var                | Default                         | Description                            |
| ----------------------- | ----- | ---------------------- | ------------------------------- | -------------------------------------- |
| `--config <file>`       | `-c`  | `SIPI_CONFIGFILE`      |                                 | Lua configuration file for server mode |
| `--serverport <n>`      |       | `SIPI_SERVERPORT`      | `80`                            | HTTP port                              |
| `--sslport <n>`         |       | `SIPI_SSLPORT`         | `443`                           | HTTPS port                             |
| `--hostname <name>`     |       | `SIPI_HOSTNAME`        | `localhost`                     | Public DNS hostname                    |
| `--keepalive <sec>`     |       | `SIPI_KEEPALIVE`       | `5`                             | HTTP keep-alive timeout in seconds     |
| `--nthreads <n>`        | `-t`  | `SIPI_NTHREADS`        | CPU cores                       | Number of worker threads               |
| `--maxpost <size>`      |       | `SIPI_MAXPOSTSIZE`     | `300M`                          | Maximum POST upload size               |
| `--imgroot <path>`      |       | `SIPI_IMGROOT`         | `./images`                      | Image repository root directory        |
| `--docroot <path>`      |       | `SIPI_DOCROOT`         | `./server`                      | Web server document root               |
| `--wwwroute <path>`     |       | `SIPI_WWWROUTE`        | `/server`                       | URL route for web server               |
| `--scriptdir <path>`    |       | `SIPI_SCRIPTDIR`       | `./scripts`                     | Directory for Lua route scripts        |
| `--tmpdir <path>`       |       | `SIPI_TMPDIR`          | `./tmp`                         | Temporary files directory              |
| `--maxtmpage <sec>`     |       | `SIPI_MAXTMPAGE`       | `86400`                         | Max age of temp files in seconds       |
| `--initscript <path>`   |       | `SIPI_INITSCRIPT`      | `./config/sipi.init.lua`        | Path to Lua init script                |
| `--cachedir <path>`     |       | `SIPI_CACHEDIR`        | `./cache`                       | Cache directory                        |
| `--cachesize <size>`    |       | `SIPI_CACHESIZE`       | `200M`                          | Maximum cache size                     |
| `--cachenfiles <n>`     |       | `SIPI_CACHENFILES`     | `200`                           | Maximum number of cached files         |
| `--cachehysteresis <f>` |       | `SIPI_CACHEHYSTERESIS` | `0.15`                          | Cache purge ratio (0.0 - 1.0)          |
| `--thumbsize <size>`    |       | `SIPI_THUMBSIZE`       | `!128,128`                      | Default thumbnail size (IIIF syntax)   |
| `--sslcert <path>`      |       | `SIPI_SSLCERTIFICATE`  | `./certificate/certificate.pem` | SSL certificate path                   |
| `--sslkey <path>`       |       | `SIPI_SSLKEY`          | `./certificate/key.pem`         | SSL key file path                      |
| `--jwtkey <string>`     |       | `SIPI_JWTKEY`          |                                 | JWT shared secret (42 chars)           |
| `--loglevel <level>`    |       | `SIPI_LOGLEVEL`        | `DEBUG`                         | Log level (see Logging section)        |

### Sentry Error Reporting

| Flag                         | Env Var                   | Description                    |
| ---------------------------- | ------------------------- | ------------------------------ |
| `--sentry-dsn <url>`         | `SIPI_SENTRY_DSN`         | Sentry DSN for error reporting |
| `--sentry-release <ver>`     | `SIPI_SENTRY_RELEASE`     | Sentry release version         |
| `--sentry-environment <env>` | `SIPI_SENTRY_ENVIRONMENT` | Sentry environment name        |

### Deprecated Options

| Flag                      | Description                                   |
| ------------------------- | --------------------------------------------- |
| `--salsah`                | Legacy flag for old SALSAH system conversions |
| `--subdirlevels <n>`      | Number of subdirectory levels (deprecated)    |
| `--subdirexcludes <dirs>` | Directories excluded from subdir calculations |
| `--pathprefix`            | Treat IIIF prefix as file path (deprecated)   |

## Environment Variables

All server options can be configured via environment variables. Environment variables override Lua configuration file values but are themselves overridden by command-line flags.

| Variable                  | CLI Flag               | Default                         | Description                  |
| ------------------------- | ---------------------- | ------------------------------- | ---------------------------- |
| `SIPI_CONFIGFILE`         | `--config`             |                                 | Configuration file path      |
| `SIPI_SERVERPORT`         | `--serverport`         | `80`                            | HTTP port                    |
| `SIPI_SSLPORT`            | `--sslport`            | `443`                           | HTTPS port                   |
| `SIPI_HOSTNAME`           | `--hostname`           | `localhost`                     | Public hostname              |
| `SIPI_KEEPALIVE`          | `--keepalive`          | `5`                             | Keep-alive timeout (seconds) |
| `SIPI_NTHREADS`           | `--nthreads`           | CPU cores                       | Worker threads               |
| `SIPI_MAXPOSTSIZE`        | `--maxpost`            | `300M`                          | Max POST size                |
| `SIPI_IMGROOT`            | `--imgroot`            | `./images`                      | Image root directory         |
| `SIPI_DOCROOT`            | `--docroot`            | `./server`                      | Document root                |
| `SIPI_WWWROUTE`           | `--wwwroute`           | `/server`                       | Web server route             |
| `SIPI_SCRIPTDIR`          | `--scriptdir`          | `./scripts`                     | Lua scripts directory        |
| `SIPI_TMPDIR`             | `--tmpdir`             | `./tmp`                         | Temporary directory          |
| `SIPI_MAXTMPAGE`          | `--maxtmpage`          | `86400`                         | Max temp file age            |
| `SIPI_INITSCRIPT`         | `--initscript`         | `./config/sipi.init.lua`        | Init script path             |
| `SIPI_CACHEDIR`           | `--cachedir`           | `./cache`                       | Cache directory              |
| `SIPI_CACHESIZE`          | `--cachesize`          | `200M`                          | Max cache size               |
| `SIPI_CACHENFILES`        | `--cachenfiles`        | `200`                           | Max cached files             |
| `SIPI_CACHEHYSTERESIS`    | `--cachehysteresis`    | `0.15`                          | Cache purge ratio            |
| `SIPI_THUMBSIZE`          | `--thumbsize`          | `!128,128`                      | Thumbnail size               |
| `SIPI_SSLCERTIFICATE`     | `--sslcert`            | `./certificate/certificate.pem` | SSL certificate              |
| `SIPI_SSLKEY`             | `--sslkey`             | `./certificate/key.pem`         | SSL key                      |
| `SIPI_JWTKEY`             | `--jwtkey`             |                                 | JWT secret                   |
| `SIPI_JPEGQUALITY`        | `--quality`            | `60`                            | JPEG quality                 |
| `SIPI_LOGLEVEL`           | `--loglevel`           | `DEBUG`                         | Log level                    |
| `SIPI_SENTRY_DSN`         | `--sentry-dsn`         |                                 | Sentry DSN                   |
| `SIPI_SENTRY_RELEASE`     | `--sentry-release`     |                                 | Sentry release               |
| `SIPI_SENTRY_ENVIRONMENT` | `--sentry-environment` |                                 | Sentry environment           |

**Configuration precedence** (highest to lowest):

1. Command-line flags
1. Environment variables
1. Lua configuration file

## Exit Codes and Error Handling

### Exit Codes

When running SIPI as a command-line image converter, the process exit code indicates whether the conversion succeeded:

- **0** — Success. The output file was written correctly.
- **1** (`EXIT_FAILURE`) — Image processing error. The image could not be read, converted, or written.

**Important for calling services:** Always check the exit code. A non-zero exit code means the output file was not produced (or is incomplete).

### Error Output

On failure, SIPI prints a short error message to **stderr** indicating the failure phase and the specific error. The format is:

```
Error <phase> image: <details>
```

Where `<phase>` is one of `reading`, `converting`, or `writing`. Example:

```
Error reading image: Unsupported JPEG colorspace YCCK (file=input.jpg, dimensions=2048x1536, components=4)
```

### Sentry Integration (CLI Mode)

When the `SIPI_SENTRY_DSN` environment variable is set, CLI conversion failures automatically send a Sentry event with rich image context. This allows developers to diagnose failures without reproducing them locally.

Each Sentry event includes:

- **Tags** (indexed, searchable, filterable in Sentry):

  - `sipi.mode` — always `cli` for command-line conversions
  - `sipi.phase` — `read`, `convert`, or `write`
  - `sipi.output_format` — the target format (e.g., `jpx`, `jpg`, `tif`, `png`)
  - `sipi.colorspace` — the image's photometric interpretation
  - `sipi.bps` — bits per sample

- **Context** ("Image" context with structured data):

  - `input_file`, `output_file` — file paths
  - `width`, `height` — image dimensions (if read successfully)
  - `channels` — number of color channels
  - `bps` — bits per sample
  - `colorspace` — photometric interpretation
  - `icc_profile_type` — ICC profile type (e.g., sRGB, AdobeRGB, CMYK)
  - `orientation` — EXIF orientation
  - `file_size_bytes` — input file size

### Common Failure Causes

| Error                                  | Meaning                                                                                                                     |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Unsupported colorspace (YCCK, unknown) | The JPEG uses a colorspace SIPI cannot convert. Re-encode the source image in sRGB.                                         |
| Unsupported bits/sample                | Only 8 and 16 bits/sample are supported. Images with other bit depths must be converted first.                              |
| Channel/colorspace mismatch            | The number of channels does not match the declared colorspace (e.g., 4 channels but RGB). The file metadata may be corrupt. |
| ICC profile incompatible               | The ICC profile does not match the channel count (e.g., CMYK profile on a 3-channel image).                                 |
| Corrupt or truncated file              | The input file is incomplete or damaged.                                                                                    |
| Unsupported TIFF tiling                | The TIFF tile configuration is inconsistent or uses unsupported bit depths.                                                 |

### Integration Notes for Calling Services

If you call SIPI CLI from another service (e.g., a Java service):

1. **Check the exit code.** Non-zero means failure — do not assume the output file exists or is valid.
1. **Parse stderr** (optional). The first line of stderr contains a human-readable error message with the failure phase and details.
1. **Set `SIPI_SENTRY_DSN`** to get full diagnostics server-side. Use the Sentry tags `sipi.phase`, `sipi.colorspace`, `sipi.bps`, and `sipi.output_format` to build alerts and filters for specific failure patterns.

## Configuration Files

SIPI's configuration file is written in [Lua](https://www.lua.org/). You can make your own configuration file by adapting `config/sipi.config.lua`.

- Check that the port number is correct and that your operating system's firewall does not block it.
- Set `imgroot` to the directory containing the files to be served.
- Create the directory `cache` in the top-level directory of the source tree.

For more information, see the comments in `config/sipi.config.lua` and the [Reference](https://sipi.io/guide/sipi/index.md) page for all configuration parameters.

### HTTPS Support

SIPI supports SSL/TLS encryption if the [OpenSSL](https://www.openssl.org/) library is installed. You will need to install a certificate; see `config/sipi.config.lua` for instructions.

### IIIF Prefixes

SIPI supports [IIIF Image API URLs](https://iiif.io/api/image/3.0/#21-image-request-uri-syntax).

If the configuration property `prefix_as_path` is set to `true`, the IIIF `prefix` portion of the URL is interpreted as a subdirectory of `imgroot`, and SIPI looks for the requested image file in that subdirectory. Otherwise, it looks for the file in `imgroot`.
