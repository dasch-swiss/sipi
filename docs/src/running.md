Running Sipi
============

After following the instructions in building, you will find the
executable `local/bin/sipi` in the source tree.

It can be run either as simple command-line image converter or as a
server.

Running Sipi As a Command-line Image Converter
----------------------------------------------

Convert an image file to another format:

    local/bin/sipi --format [output format] --fileIn [input file] [output file]

Compare two image files:

    local/bin/sipi --compare file1 file2 

Running Sipi As a Server
------------------------

    local/bin/sipi --config [config file]

### Logging

SIPI uses two logging modes depending on how it is running:

- **CLI mode** (`--file`, `--compare`, `--query`): Plain text output.
  Errors go to **stderr**, informational messages go to **stdout**.
  This is the standard Unix convention for command-line tools.

- **Server mode** (`--config`): JSON-formatted log lines go to **stdout**.
  This follows container best practices — Docker, Kubernetes, and log
  collectors (Grafana Loki, Fluentd) expect structured logs on stdout.
  Each line is a JSON object: `{"level": "INFO", "message": "..."}`.

### Log Levels

SIPI supports the following log levels (in order of increasing severity):

| Level | Description |
| ----- | ----------- |
| `DEBUG` | Detailed diagnostic information. **Suppressed by default** in all modes. |
| `INFO` | Normal operational messages (routes added, server started, migrations). |
| `NOTICE` | Significant but normal events. |
| `WARNING` | Something unexpected but recoverable (e.g., failed XMP parse, incomplete metadata write). |
| `ERR` | Errors that affect a specific operation (e.g., image processing failure, ICC error). |
| `CRIT` | Critical errors. |
| `ALERT` | Conditions requiring immediate attention. |
| `EMERG` | System-wide emergencies. |

**For production:** The default configuration (DEBUG suppressed, all other levels
emitted) is appropriate for production use. No additional log level configuration
is needed. Warnings and errors will appear in the log output alongside
informational messages about server startup and routing.

Command-line Options
--------------------

    Options:
      --config filename, -c filename
                        Configuration file for web server.

      --file fileIn, -f fileIn
                        input file to be converted. Usage: sipi [options] -f fileIn
                        fileout

      --format Value, -F Value
                        Output format Value can be: jpx,jpg,tif,png.

      --ICC Value, -I Value
                        Convert to ICC profile. Value can be:
                        none,sRGB,AdobeRGB,GRAY.

      --quality Value, -q Value
                        Quality (compression). Value can any integer between 1 and
                        100

      --region x,y,w,h, -r x,y,w,h
                        Select region of interest, where x,y,w,h are integer values

      --Reduce Value, -R Value
                        Reduce image size by factor Value (cannot be used together
                        with --size and --scale)

      --size w,h -s w,h
                        Resize image to given size w,h (cannot be used together with
                        --reduce and --scale)

      --Scale Value, -S Value
                        Resize image by the given percentage Value (cannot be used
                        together with --size and --reduce)

      --skipmeta Value, -k Value
                        Skip the given metadata. Value can be none,all
                    
      --topleft fileIn fileOut
                        Enforce orientation TOPLEFT.

      --mirror Value, -m Value
                        Mirror the image. Value can be: none,horizontal,vertical

      --rotate Value, -o Value
                        Rotate the image. by degree Value, angle between (0:360)

      --salsah, -s
                        Special flag for SALSAH internal use

      --compare file1 file2 or -C file1 file2
                        Compare two files

      --watermark file, -w file
                        Add a watermark to the image

      --serverport Value, -p Value
                        Port of the web server

      --nthreads Value, -t Value
                        Number of threads for web server

      --imgroot Value, -i Value
                        Root directory containing the images for the web server

      --loglevel Value, -l Value
                        Logging level Value can be:
                        DEBUG,INFO,NOTICE,WARNING,ERR,CRIT,ALERT,EMERG

      --help
                        Print usage and exit.

Exit Codes and Error Handling
-----------------------------

### Exit Codes

When running SIPI as a command-line image converter, the process exit code indicates
whether the conversion succeeded:

- **0** — Success. The output file was written correctly.
- **1** (`EXIT_FAILURE`) — Image processing error. The image could not be read,
  converted, or written.

**Important for calling services:** Always check the exit code. A non-zero exit code
means the output file was not produced (or is incomplete).

### Error Output

On failure, SIPI prints a short error message to **stderr** indicating the failure
phase and the specific error. The format is:

    Error <phase> image: <details>

Where `<phase>` is one of `reading`, `converting`, or `writing`. Example:

    Error reading image: Unsupported JPEG colorspace YCCK (file=input.jpg, dimensions=2048x1536, components=4)

### Sentry Integration (CLI Mode)

When the `SIPI_SENTRY_DSN` environment variable is set, CLI conversion failures
automatically send a Sentry event with rich image context. This allows developers
to diagnose failures without reproducing them locally.

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

| Error | Meaning |
| ----- | ------- |
| Unsupported colorspace (YCCK, unknown) | The JPEG uses a colorspace SIPI cannot convert. Re-encode the source image in sRGB. |
| Unsupported bits/sample | Only 8 and 16 bits/sample are supported. Images with other bit depths must be converted first. |
| Channel/colorspace mismatch | The number of channels does not match the declared colorspace (e.g., 4 channels but RGB). The file metadata may be corrupt. |
| ICC profile incompatible | The ICC profile does not match the channel count (e.g., CMYK profile on a 3-channel image). |
| Corrupt or truncated file | The input file is incomplete or damaged. |
| Unsupported TIFF tiling | The TIFF tile configuration is inconsistent or uses unsupported bit depths. |

### Integration Notes for Calling Services

If you call SIPI CLI from another service (e.g., a Java service):

1. **Check the exit code.** Non-zero means failure — do not assume the output file
   exists or is valid.
2. **Parse stderr** (optional). The first line of stderr contains a human-readable
   error message with the failure phase and details.
3. **Set `SIPI_SENTRY_DSN`** to get full diagnostics server-side. Use the Sentry
   tags `sipi.phase`, `sipi.colorspace`, `sipi.bps`, and `sipi.output_format` to
   build alerts and filters for specific failure patterns.

Configuration Files
-------------------

Sipi's configuration file is written in [Lua](https://www.lua.org/). You
can make your own configuration file by adapting
`config/sipi.config.lua`.

-   Check that the port number is correct and that your operating
    system's firewall does not block it.
-   Set `imgroot` to the directory containing the files to be served.
-   Create the directory `cache` in the top-level directory of the
    source tree.

For more information, see the comments in `config/sipi.config.lua`.

### Using Sipi with Knora

If you are using Sipi with [Knora](http://www.knora.org/), you can adapt
`config/sipi.knora-config.lua`.

### HTTPS Support

Sipi supports SSL/TLS encryption if the
[OpenSSL](https://www.openssl.org/) library is installed. You will need
to install a certificate; see `config/sipi.config.lua` for instructions.

### IIIF Prefixes

Sipi supports [IIIF Image API
URLs](https://iiif.io/api/image/3.0/#21-image-request-uri-syntax).

If the configuration property `prefix_as_path` is set to `true`, the
IIIF `prefix` portion of the URL is interpreted as a subdirectory of
`imgroot`, and Sipi looks for the requested image file in that
subdirectory. Otherwise, it looks for the file in `imgroot`.
