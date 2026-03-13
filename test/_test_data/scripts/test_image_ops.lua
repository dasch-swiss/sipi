-- Test script for image manipulation operations (crop, scale, rotate).
-- Returns JSON with the resulting image dimensions after the operation.
--
-- Query parameters:
--   op: "crop", "scale", or "rotate"
--   param: operation parameter (region string, size string, or angle)
--   file: image file path relative to imgroot (e.g. "unit/lena512.jp2")

require "send_response"

local op = server.get['op']
local param = server.get['param']
local file = server.get['file']

if not op or not param or not file then
    send_error(400, "Missing required query parameters: op, param, file")
    return
end

local filepath = config.imgroot .. '/' .. file

local success, img = SipiImage.new(filepath)
if not success then
    send_error(500, "Failed to load image: " .. tostring(img))
    return
end

-- Get original dimensions
local success, orig_dims = img:dims()
if not success then
    send_error(500, "Failed to get original dimensions: " .. tostring(orig_dims))
    return
end

-- Apply the requested operation
if op == "crop" then
    local success, err = img:crop(param)
    if not success then
        send_error(500, "crop failed: " .. tostring(err))
        return
    end
elseif op == "scale" then
    local success, err = img:scale(param)
    if not success then
        send_error(500, "scale failed: " .. tostring(err))
        return
    end
elseif op == "rotate" then
    local angle = tonumber(param)
    if not angle then
        send_error(400, "rotate param must be a number")
        return
    end
    local success, err = img:rotate(angle)
    if not success then
        send_error(500, "rotate failed: " .. tostring(err))
        return
    end
else
    send_error(400, "Unknown op: " .. op)
    return
end

-- Get resulting dimensions
local success, result_dims = img:dims()
if not success then
    send_error(500, "Failed to get result dimensions: " .. tostring(result_dims))
    return
end

send_success({
    op = op,
    param = param,
    original = { nx = orig_dims.nx, ny = orig_dims.ny },
    result = { nx = result_dims.nx, ny = result_dims.ny }
})
