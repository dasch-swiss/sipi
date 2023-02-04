
require "send_response"

server.setBuffer()
server.sendHeader("Content-Type", "text/html")

local test_image_path = config.imgroot .. "/unit/img_exif_gps.jpg"

local success, img = SipiImage.new(test_image_path)
if not success then
    send_error(500, "loading image failed: " .. test_image_path)
    return false
end

local width
success, width = img:exif("ImageWidth")
if not success then
    send_error(500, "Getting Exif.Image.ImageWidth failed")
    return false
end

local height
success, height = img:exif("ImageLength")
if not success then
    send_error(500, "Getting Exif.Image.ImageLength failed")
    return false
end

local ori
success, ori = img:exif("Orientation")
if not success then
    send_error(500, "Getting Exif.Image.Orientation failed")
    return false
end

local compression
success, compression = img:exif("Compression")
if not success then
    send_error(500, "Getting Exif.Image.Compression failed")
    return false
end

local photometric
success, photometric = img:exif("PhotometricInterpretation")
if not success then
    send_error(500, "Getting Exif.Image.PhotometricInterpretation failed")
    return false
end

local spp
success, spp = img:exif("SamplesPerPixel")
if not success then
    send_error(500, "Getting Exif.Image.SamplesPerPixel failed")
    return false
end

local resunit
success, resunit = img:exif("ResolutionUnit")
if not success then
    send_error(500, "Getting Exif.Image.ResolutionUnit failed")
    return false
end

local planarconfig
success, planarconfig = img:exif("PlanarConfiguration")
if not success then
    send_error(500, "Getting Exif.Image.PlanarConfiguration failed")
    return false
end

local datetime
success, datetime = img:exif("DateTime")
if not success then
    send_error(500, "Getting Exif.Image.DateTime failed")
    return false
end

local make
success, make = img:exif("Make")
if not success then
    send_error(500, "Getting Exif.Image.Make failed")
    return false
end

local model
success, model = img:exif("Model")
if not success then
    send_error(500, "Getting Exif.Image.Model failed")
    return false
end

local xres
success, xres = img:exif("XResolution")
if not success then
    send_error(500, "Getting Exif.Image.XResolution failed")
    return false
end

local yres
success, yres = img:exif("YResolution")
if not success then
    send_error(500, "Getting Exif.Image.YResolution failed")
    return false
end

local exposure
success, exposure = img:exif("ExposureTime")
if not success then
    send_error(500, "Getting Exif.Image.ExposureTime failed")
    return false
end

local imagedescription
success, imagedescription = img:exif("ImageDescription")
if not success then
    send_error(500, "Getting Exif.Image.ImageDescription failed")
    return false
end

local hostcomputer
success, hostcomputer = img:exif("HostComputer")
if not success then
    send_error(500, "Getting Exif.Image.HostComputer failed")
    return false
end

local copyright
success, copyright = img:exif("Copyright")
if not success then
    send_error(500, "Getting Exif.Image.Copyright failed")
    return false
end

local imageid
success, imageid = img:exif("ImageID")
if not success then
    send_error(500, "Getting Exif.Image.ImageID failed")
    return false
end

local resobj = {
    ori = ori,
    compression = compression,
    datetime = datetime,
    make = make,
    model = model,
    photometric = photometric,
    spp = spp,
    resunit = resunit,
    planarconfig = planarconfig,
    width = width,
    height = height,
    xres = xres,
    yres = yres,
    exposure = exposure,
    imagedescription = imagedescription,
    hostcomputer = hostcomputer,
    copyright = copyright,
    imageid = imageid
}
send_success(resobj)
return true
