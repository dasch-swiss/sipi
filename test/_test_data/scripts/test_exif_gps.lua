
require "send_response"

server.setBuffer()
server.sendHeader("Content-Type", "text/html")

local test_image_path = config.imgroot .. "/unit/img_exif_gps.jpg"

local success, img = SipiImage.new(test_image_path)
if not success then
    send_error(500, "loading image failed: " .. test_image_path)
    return false
end

local tags = {
    "DocumentName",
    "ImageDescription",
    "Make",
    "Model",
    "Orientation",
    "XResolution",
    "YResolution",
    "PageName",
    "XPosition",
    "YPosition",
    "ResolutionUnit",
    "PageNumber",
    "Software",
    "ModifyDate",
    "DateTime",
    "Artist",
    "HostComputer",
    "TileWidth",
    "TileLength",
    "ImageID",
    "BatteryLevel",
    "Copyright",
    "ImageNumber",
    "ImageHistory",
    "UniqueCameraModel",
    "CameraSerialNumber",
    "LensInfo",
    "CameraLabel",
    "ExposureTime",
    "FNumber",
    "ExposureProgram",
    "SpectralSensitivity",
    "ISOSpeedRatings",
    "SensitivityType",
    "StandardOutputSensitivity",
    "RecommendedExposureIndex",
    "ISOSpeed",
    "ISOSpeedLatitudeyyy",
    "ISOSpeedLatitudezzz",
    "DateTimeOriginal",
    "DateTimeDigitized",
    "OffsetTime",
    "OffsetTimeOriginal",
    "OffsetTimeDigitized",
    "ShutterSpeedValue",
    "ApertureValue",
    "BrightnessValue",
    "ExposureBiasValue",
    "MaxApertureValue",
    "SubjectDistance",
    "MeteringMode",
    "LightSource",
    "Flash",
    "FocalLength",
    "UserComment",
    "SubSecTime",
    "SubSecTimeOriginal",
    "SubSecTimeDigitized",
    "Temperature",
    "Humidity",
    "Pressure",
    "WaterDepth",
    "Acceleration",
    "CameraElevationAngle",
    "RelatedSoundFile",
    "FlashEnergy",
    "FocalPlaneXResolution",
    "FocalPlaneYResolution",
    "FocalPlaneResolutionUnit",
    "SceneCaptureType",
    "GainControl",
    "Contrast",
    "Saturation",
    "Sharpness",
    "SubjectDistanceRange",
    "ImageUniqueID",
    "OwnerName",
    "SerialNumber",
    "LensInfo",
    "LensMake",
    "LensModel",
    "LensSerialNumber",
}

local success, value
local resobj = {}
for _, tagname in ipairs(tags) do
    success, value = img:exif(tagname)
    if not success then
        resobj[tagname] = "--undefined--"
    else
        resobj[tagname] = value
    end
end


send_success(resobj)
return true
