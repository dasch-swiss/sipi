--
-- Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
-- Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
-- This file is part of Sipi.
-- Sipi is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Affero General Public License as published
-- by the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
-- Sipi is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
-- Additional permission under GNU AGPL version 3 section 7:
-- If you modify this Program, or any covered work, by linking or combining
-- it with Kakadu (or a modified version of that library), containing parts
-- covered by the terms of the Kakadu Software Licence, the licensors of this
-- Program grant you additional permission to convey the resulting work.
-- See the GNU Affero General Public License for more details.
-- You should have received a copy of the GNU Affero General Public
-- License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.

require "send_response"

result = {}

-- Check mimetypes of files

local test_files = {
    {
        filename = 'big.pdf',
        expected_mimetype = "application/pdf"
    },
    {
        filename = 'test.csv',
        expected_mimetype = "text/csv"
    }
}

for i, fileitem in ipairs(test_files) do
    local success, mimetype = server.file_mimetype(config.imgroot .. "/unit/" .. fileitem.filename)
    if not success then
        send_error(400, "Couldn't get mimetype: " .. mimetype)
        return -1
    end

    if mimetype.mimetype ~= fileitem.expected_mimetype then
        send_error(400, "File has '" .. mimetype.mimetype .. "', but expected '" .. fileitem.expected_mimetype .. "'!")
        return -1
    end
    table.insert(result, { {filename = fileitem.filename, mimetype = mimetype}, "OK" })
end

for i, fileitem in ipairs(test_files) do
    local tmpname = config.imgroot .. "/unit/" .. fileitem.filename
    local success, is_ok = server.file_mimeconsistency(tmpname, fileitem.expected_mimetype, fileitem.filename)
    if not success then
        send_error(400, "Couldn't get mimetype consistency: " .. is_ok)
        return -1
    end

    table.insert(result, { {filename = fileitem.filename, is_ok = (is_ok and "OK" or "NOT OK")}, "OK" })
end


success, mimeinfo = server.file_mimetype(config.imgroot .. '/unit/' .. 'lena512.tif')
if not success then
    server.send_error(500, mimetype)
    return false
end

if mimeinfo['mimetype'] == "image/tiff" then
    server.sendStatus(200)
    return true
else
    server.sendStatus(500)
    return 200
end

