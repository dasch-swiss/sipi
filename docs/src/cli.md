# SIPI Command Line Interface

- `-h`, `--help`: Display a short help with all options available
- `-v`, `--version`: Display the version of the program

## Convert a file

```bash
$ sipi convert [options] input_file output_file
```
### General options:
- `-F <fmt>`, `--format <fmt>`: The format of the output file. Valid are `jpx`, `jp2`, `jpg`, and `png`.
- `-r <x> <y> <nx> <ny>`, `--region <x> <y> <nx> <ny>`: Selects a region of interest that should be converted. Needs 4 integer values: `left_upper_corner_X`, `left_upper_corner_Y`, `width`, `height`.
- `-s <iiif-size>`, `--size <iif-size>`: The size of the resulting image. The option requires a string parameter formatted according to the size-syntax of IIIF [see IIIF-Size](https://iiif.io/api/image/3.0/#42-size). Not giving this parameters results in having the maximalsize (as the value `"max"`would give).
- `-s <num>`, `--scale <num>`: Scaling the image size by the given number (interpreted as percentage). Percentage must be given as integer value. It may be bigger than 100 to upscale an image.
- `-R <num>`, `--reduce <num>`: Reduce the size of the image by the given factor. Thus `-R 2`would resize the image to half of the original size. Using `--reduce` is usually much faster than using `--scale`, e.g. `--reduce 2` is faster than `--scale 50`.
- `-m <val>`, `--mirror <val>`: Takes either `horizontal` or `vertical`as parameter to mirror the image appropriately.
- `-o <angle>`, `--rotate <angle>`: Rotates the image by the given angle. The angle must be a floating point (or integer) value between `0.0`and `w60.0`.
- `-k`, `--skipmeta`: Strip all metadata from inputfile.
- `-w <filepath>`, `--watermark <filepath>`: Overlays a watermark to the output image. <filepath> must be a single channel, gray valued TIFF. That is, the TIFF file must have the following tag values: SAMPLESPERPIXEL = 1, BITSPERSAMPLE = 8, PHOTOMETRIC = PHOTOMETRIC_MINISBLACK.

### JPEG options:
- `-q <num>`, `--quality <num>`: Only used for the JPEG format. Ignored for all other formats. It's a number between 1 and 100, where 1 is equivalent to the highest compression ratio and lowest quality, 100 to the lowest compression ratio and highest quality of the output image.

### TIFF options:
- `-n <num>`, `--pagenum <num>`: Only for input files in multi-page format: sets the page that should be converted. Ignored for all other input file formats.

### JPEG2000 Specific Options:
Usually, the SIPI command line tool is used to create JPEG2000 images suitable for a IIIF repository. SIPI supports the following JPEG2000 specific options. For a in detail description of these options consult the kakadu documentation!

- `--Sprofile <profile>`: The following JPEG2000 profiles are supported: `PROFILE0`, `PROFILE1`, `PROFILE2`, `PART2`, `CINEMA2K`, `CINEMA4K`, `BROADCAST`, `CINEMA2S`, `CINEMA4S`, `CINEMASS`, `IMF`. **Default: `PART2`**.
- `--rates <string>`: One or more bit-rates (see kdu_compress help!). A value "-1" may be used in place of the first bit-rate in the list to indicate that the final quality layer should include all compressed bits.
- `--Clayers <num>`:Number of quality layers. **Default: 8**.
- `--Clevels <num>`: Number of wavelet decomposition levels, or stages. **Default: 8**.
- `--Corder <val>`: Progression order. The four character identifiers have the following interpretation: L=layer; R=resolution; C=component; P=position. The first character in the identifier refers to the index which progresses most slowly, while the last refers to the index which progresses most quickly. Thus must be one of `LRCP`, `RLCP`, `RPCL`, `PCRL`, `CPRL`, **Default: `RPCL`**.
- `--Stiles <string>`: Tiles dimensions `"{tx,ty}"`. **Default: `"{256,256}"`**.
- `--Cprecincts <string>`: Precinct dimensions `"{px,py}"` (must be powers of 2). __Default: `"{256,256}"`__.
- `--Cblk <string>`: Nominal code-block dimensions `"{dx,dy}"`(must be powers of 2, no less than 4 and no greater than 1024, whose product may not exceed 4096). Default: `"{64,64}"`.
- `--Cuse_sop <val>`: Include SOP markers (i.e., resync markers). **Default: yes**.

## Query information about a file

Print Information about File and Metadata.

```bash
$ sipi query file
```

## Compare two images (pixel wise)
The images may have different formats. If the images have exactly the same pixels, they are considered identical. Metadata is ignored for comparison.

```bash
$ sipi compare file1 file2
```

## Using SIPI as IIIF Media Server
In order to use SIPI as IIIF media server, some setup work has to be done. The *configuration* of SIPI can be done using a configuration file (that is written in LUA) and/or using environment variables, and/or command line options.

The priority is as follows: *`configuration file parameters` are overwritten by `environment variables` are overwritten by `command line options`*.

The SIPI server requires a few directories to be setup and listed in the configuration file. Then the SIPI server is launched as follows:

```bash
$ sipi server --config /path/to/config-file.lua
```
