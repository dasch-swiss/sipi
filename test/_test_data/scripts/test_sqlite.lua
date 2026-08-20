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

function test_db()
    local db = sqlite("./test.db", "RW")
    local qry = db << "SELECT * FROM image WHERE id = ?1 OR id = ?2 ORDER BY id"
    local id = 512
    local result = {}
    local row = qry(id, 1024)
    while row do
        result[row[0]] = row[1]
        row = qry()
    end
    return result
end

function err (x)
    send_error(500, x)
    return false
end

local status, res = xpcall(test_db, err)
if not status then
    send_error(500, res)
    return false
else
    local resobj = {}
    resobj["result"] = res
    send_success(resobj)
end

