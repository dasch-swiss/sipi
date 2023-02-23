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

-- upload script for binary files (currently only images) from Knora
--

require "send_response"

function table.contains(table, element)
  for _, value in pairs(table) do
    if value == element then
      return true
    end
  end
  return false
end

myimg = {}
newfilename = {}
iiifurls = {}

if server.secure then
    protocol = 'https://'
else
    protocol = 'http://'
end


for imgindex,imgparam in pairs(server.uploads) do

    --
    -- first we check the mimetype consistency
    --
    local success
    success, mime_ok = server.file_mimeconsistency(imgindex)
    if not success then
        server.log(newfilepath, server.loglevel.error)
        send_error(500, mime_ok)
        return false
    end


    if not mime_ok then
        success, mimetypeobj = server.file_mimetype(imgindex)
        if not success then
            server.log("Couldn't determine mimetype!", server.loglevel.error)
            send_error(500, mime_ok)
            return false
        end
        mimetype = mimetypeobj.mimetype
    else
        mimetype = imgparam["mimetype"]
    end

    --
    -- generate a UUID
    --
    local uuid62
    success, uuid62 = server.uuid62()
    if not success then
        server.log(uuid62, server.loglevel.error)
        send_error(500, uuid62)
        return false
    end


    if mimetype == "image/tiff" or
            mimetype == "image/jpeg" or
            mimetype == "image/png" or
            mimetype == "image/jpx" or
            mimetype == "image/jp2" then
        --
        -- create a new Lua image object. This reads the image into an
        -- internal in-memory representation independent of the original
        -- image format.
        --
        success, myimg[imgindex] = SipiImage.new(imgindex)
        if not success then
            server.log(myimg[imgindex], server.loglevel.error)
            send_error(500, myimg[imgindex])
            return false
        end

        filename = imgparam["origname"]
        filebody = filename:match("(.+)%..+")
        newfilename[imgindex] = "_" .. filebody .. '.jp2'

        iiifurls[uuid62 .. ".jp2"] = protocol .. server.host .. '/unit/' ..newfilename[imgindex]
        iiifurls["filename"] = newfilename[imgindex]

        --
        -- here we add the subdirs that are necessary if Sipi is configured to use subdirs
        --
        success, newfilepath = helper.filename_hash(newfilename[imgindex]);
        if not success then
            server.log(newfilepath, server.loglevel.error)
            send_error(500, newfilepath)
            return false
        end


        --
        -- Set orientation to topleft
        --
        myimg[imgindex]:topleft()

        --
        -- Create the destination path
        --
        fullfilepath = config.imgroot .. '/unit/' .. newfilepath

        --
        -- write the file to the destination
        --
        local status, errmsg = myimg[imgindex]:write(fullfilepath)
        if not status then
            server.print('Error converting image to j2k: ', filename, ' ** ', errmsg)
        end
    else
        filename = "_" .. imgparam["origname"]
        --
        -- here we add the subdirs that are necessary if Sipi is configured to use subdirs
        --
        success, newfilepath = helper.filename_hash(filename)
        if not success then
            server.log(newfilepath, server.loglevel.error)
            send_error(500, newfilepath)
            return false
        end

        fullfilepath = config.imgroot .. '/unit/' .. newfilepath
        local errormsg
        success, errormsg = server.copyTmpfile(imgindex, fullfilepath)
        if not success then
            server.log(errormsg, server.loglevel.error)
            send_error(500,errormsg)
            return false
        end
        iiifurls[" fullfilepath"] =  fullfilepath

        iiifurls[filename] = protocol .. server.host .. '/unit/' .. newfilepath
        iiifurls["filename"] = filename
    end

end

send_success(iiifurls)
