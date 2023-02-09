---
--- Testing normalization of image orientation
---
server.setBuffer()
server.sendHeader("Content-Type", "text/html")

local test_image_path = config.imgroot .. "/unit/image_with_rotation.jpg"

local success, img = SipiImage.new(test_image_path)
if not success then
    server.log("loading image failed: " .. tostring(test_image_path), server.loglevel.LOG_ERR)
    server.sendStatus(500)
    return false
end

-- Normalize image orientation to top-left --
success, error_msg = img:topleft()
if not success then
    server.log("normalize image orientation failed for: " .. tostring(test_image_path) .. ": " .. tostring(error_msg), server.loglevel.LOG_ERR)
    server.sendStatus(500)
    return
end

-- Check orientation normalization --
status, orientation_after_normalization = img:exif("Orientation")
if (tostring(orientation_after_normalization) ~= "1") then
    server.log("Orientation was not normalized, got " .. tostring(orientation_after_normalization))
    server.sendStatus(500)
    return false
end

server.sendStatus(200)
return true
