# Lua image functions
Through Lua scripting, SIPI allows a wide area of utilities to analyze, manipulate and convert images to/from
different formats. This functionality allows to use SIPI e.g. for offering image upload and converting these images
into IIIF conformant long-term storage formats (e.g. JPEG2000). It allows to programmatically modify an image
before delivering it to the client, or to extract data from the images.

The basic concept is a specialized Lua image object that offers all methods to manipulate images.

### SipiImage.new(filename)
This method creates a new image object by reading an image file that has to be located somewhere on the SIPI server.

The simple forms are:

    img = SipiImage.new("filepath")
    img = SipiImage.new(index)

The first variant opens a file given by "filepath", the second variant
opens an uploaded file directly using the integer index to the uploaded
files.

If the index of an uploaded file is passed as an argument, this method
adds additional metadata to the `SipiImage` object that is constructed:
the file's original name, its MIME type, and its SHA256 checksum. When
the `SipiImage` object is then written to another file, this metadata
will be stored in an extra header record.

If a filename is passed, the method does not add this metadata.

The more complex form is as follows:

    img = SipiImage.new("filename", {
            region=<iiif-region-string>,
            size=<iiif-size-string>,
            reduce=<integer>,
            original=origfilename,
            hash="md5"|"sha1"|"sha256"|"sha384"|"sha512"
          })

This creates a new Lua image object and loads the given image into. The
second form allows to indicate a region, the size or a reduce factor and
the original filename. The `hash` parameter indicates that the given
checksum should be calculated out of the pixel values and written into
the header. All parameters are optional, but at least one has to given.
The meaning of the parameters are:

- `region`: A region in IIIF format the image should be cropped to.
- `size`: The size of the resulting image as valid IIIF size string.
- `reduce`: An much faster alternative to size, if the image size will be
  reduced by a integer factor (2=half size, 3=one third size etc.)
- `original`: The original file name that should be recorded in the metadata
- `hash`: The Hash algorithm that will be used for the hash of the pixel values. Valid
  entries are `md5`, `sha1`, `sha256`, `sha384` and `sha512`.

For example to read an image and include the SIPI preservation metadata,
the function is called as follows:

    SipiImage.new("path_to_file", { original="my_image.tif", hash="md5" }
    
This call will include the preservation metadata (please note that in this case
the original filename is mandatory, since Lua has know direct knowledge about the
original filename. The filepath given as first parameter must not and normally
does not correspond to the original filename). The `hash`-parameter indicates to
use the md5-algorithm for the has of the pixel values.

### SipiImage.dims()

      success, dims = img.dims()
      if success then
          server.print('nx=', dims.nx, ' ny=', dims.ny, ' ori=', dims.orientation)
      end

This method returns basic information about the image. It returns a Lua table withg the following items:
- _nx_: Number of pixels in X direction (image width)
- _ny_: Number of pixels in Y direction (image height)
- _orientation_: Orientation of image which is an integer with the following meaning:
  - _1_: (TOPLEFT) The 0th row represents the visual top of the image, and the 0th column represents the visual left-hand side.
  - _2_: (TOPRIGHT) The 0th row represents the visual top of the image, and the 0th column represents the visual right-hand side.
  - _3_: (BOTRIGHT) The 0th row represents the visual bottom of the image, and the 0th column represents the visual right-hand side.
  - _4_: (BOTLEFT) The 0th row represents the visual bottom of the image, and the 0th column represents the visual left-hand side.
  - _5_: (LEFTTOP) The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual top.
  - _6_: (RIGHTTOP) The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual top.
  - _7_: (RIGHTBOT) The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual bottom.
  - _8_: (LEFTBOT) The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual bottom.

### SipiImage.exif(&lt;EXIF-parameter-name&gt;)

    success, value-or-errormsg = img:exit(<EXIF-parameter-name>)

Return the value of an exif parameter. The following EXIF parameters are supported:
- _"Orientation"_: Orientation (integer)
- _"Compression"_: Compression method (integer)
- _"PhotometricInterpretation"_: The photometric interpretation (integer)
- _"SamplesPerPixel"_: Samples per pixel (integer)
- _"ResolutionUnit"_: 1=none, 2=inches, 3=cm (integer)
- _"PlanarConfiguration"_: Planar configuration, 1=chunky, 2=planar (integer)
- _"DocumentName"_: Document name (string)
- _"Make"_: Make of camera or scanner (string)
- _"Model"_: Model of camera or scanner (string)
- _"Software"_: Software used for capture (string)
- _"Artist"_: Artist that created the image (string)
- _"DateTime"_: Date and time of creation (string)
- _"ImageDescription"_: Image description
- _"Copyright"_: Copyright info
- 

### SipiImage.crop(&lt;iiif-region-string&gt;)

    success, errormsg = img.crop(<IIIF-region-string>)

Crops the image to the given rectangular region. The parameter must be a
valid IIIF-region string.

### SipiImage.scale(&lt;iiif-size-string&gt;)

    success, errormsg = img.scale(<iiif-size-string>)

Resizes the image to the given size as IIIF-conformant size string.

### SipiImage.rotate(&lt;iiif-rotation-string&gt;)

    success, errormsg = img.rotate(<iiif-rotation-string>)

Rotates and/or mirrors the image according the given iiif-conformant rotation string.

### SipiImage.topleft()
Rotates an image to the standard TOPLEFT orientation if necessary. Please note
that viewers using tiling (e.g. [openseadragon](https://openseadragon.github.io)) require images in TOPLEFT rotation.
Thus, it is highly recommended that all images served by SIPI IIIF will be set to TOPLEFT orientation. This process may
involve rotation of 90, 180 or 270 degrees and possible mirroring which does _not_ change the pixel values through interpolation.

### SipiImage.watermark(wm-file-path)

    success, errormsg = img.watermark(wm-file-path)

Applies the given watermark file to the image. The watermark file must
be a single channel 8-Bit gray value TIFF file.

### SipiImage.write(filepath, [compression_params])

    success, errormsg = img.write(filepath)
    success, errormsg = img.write('HTTP.jpg')

The first version write the image to a file in the SIPI server, the second writes the file
to the HTTP connection (which is done whenever the basename of the output file is `HTTP `):


Parameters:

- `filepath`: Path to output file. The file format is determined by the filename extension. Supported are
    -   `jpg` : writes a JPEG file
    -   `tif` : writes a TIFF file
    -   `png` : writes a PNG file
    -   `jpx` : writes a JPEG2000 file
    -   `webp` : writes a WebP file
    -   `gif` : writes a GIF file
    -   `pdf` : writes a PDF file

- `compression_params`: (optional) An optional Lua table with compression parameters (which are dependent on the
  chosen output file format!) can be given. All compression parameters are optional. But if a compression parameter
  table is give, it must have at least one entry.
  - JPEG format:
    - `quality`: Number between 1 and 100 (1 highest compression, worst quality, 100 lowest compression, best quality)      
  - JPEG2000 format:
    - `Sprofile`: Any of `PROFILE0`, `PROFILE1`, `PROFILE2`, `PART2`, `CINEMA2K`, `CINEMA4K`, `BROADCAST`,
      `CINEMA2S`, `CINEMA4S`, `CINEMASS`, `IMF`. Defaults to `PART2`.
    - `Creversible`: Use the reversible compression algorithms of JPEG2000. Must be string `yes` or `no`. Defaults
          to `yes`.
    - `Clayers`: Number of layers to use.
    - `Clevels`: Number of levels to use.
    - `Corder`: Ordering of file components. Must be one of the following strings: `LRCP`, `RLCP`, `RPCL`, `PCRL` or
      `CPRL`.
    - `Cprecincts`: A kakadu conformant precinct string.
    - `rates`: rates string as used in kakadu.
    

### SipiImage.send(format)

    success, errormsg = img.send(format)

Sends the file to the HTTP connection. Supported format strings:

-   `jpg` : sends a JPEG file
-   `tif` : sends a TIFF file
-   `png` : sends a PNG file
-   `jpx` : sends a JPEG2000 file
-   `webp` : sends a WebP file
-   `gif` : sends a GIF file

### SipiImage.mimetype_consistency(mimetype, filename)

```lua
success, consistent = img:mimetype_consistency(mimetype, original_filename)
```

This method checks if the supplied MIME type (e.g. received from the browser during upload), the file's
magic number ([file signature](https://en.wikipedia.org/wiki/List_of_file_signatures)), and the file extension are
consistent.

**Parameters:**

- `mimetype` (string): The expected MIME type (e.g., `"image/tiff"`).
- `original_filename` (string): The original filename with extension (e.g., `"photo.tif"`).

**Returns:** `(true, boolean)` on success where the boolean indicates consistency, or `(false, error_message)` on failure.

Please note that MIME type handling can be quite complex, since the correspondence between file extensions and
MIME types is not unambiguous. In addition the file signature cannot identify all MIME types. For example, a "comma
separated values" file (extension `.csv`) can have a MIME type of `application/csv`, `text/csv`, `text/x-csv`,
`application/vnd.ms-excel` and more. However, the file signature will usually return `text/plain`. SIPI tries to cope
with these ambiguities.

### Example: Image Processing Pipeline

```lua
-- Read an image, crop, scale, rotate, and write to a new format
img = SipiImage.new("input.tif")
img:crop("100,100,500,500")
img:scale("400,")
img:rotate("90")
img:write("output.jpx")
```

```lua
-- Process an uploaded file and send the result via HTTP
img = SipiImage.new(1)  -- first uploaded file
img:topleft()
img:scale("!800,800")
img:send("jpg")
```
