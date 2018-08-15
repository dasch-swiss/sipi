print("---- GET resources script ----")

-- Required external script files
require "./model/resource"
require "./model/parameter"
require "./model/file"

function generateFileName(data)
    local year, title, identifier, ending, dump, errMsg

    if (data["date_start"] == data["date_end"]) then
        year = data["date_start"]
    else
        year = data["date_start"] .. "-" .. data["date_end"]
    end

    title, dump = string.gsub(data["title"], " ", "-")

    local startVal, endVal = string.find(data["filename"], "%.")
    if (startVal ~= nil) and (endVal ~= nil) then
        ending = string.sub(data["filename"], endVal+1, #data["filename"])
    else
        errMsg = 500
    end

    return year .. "_" .. title .. "." .. ending, errMsg
end

-- Function definitions
function getResources()
    local table1 = {}

    table1["data"] = readAllRes({})

    if #table1["data"] > 0 then
        table1["status"] = "successful"
    else
        table1["status"] = "no data were found"
        table1["data"] = {{}}
    end

    server.setBuffer()

    local success, jsonstr = server.table_to_json(table1)
    if not success then
        server.sendStatus(500)
        server.log(jsonstr, server.loglevel.err)
        return false
    end

    server.sendHeader('Content-type', 'application/json')
    server.sendStatus(200)
    server.print(jsonstr)
end

function getResource()
    local table1 = {}
    local id = string.match(uri, "%d+")

    table1["data"] = readRes(id)

    if (table1["data"] ~= nil) then
        table1["status"] = "successful"
    else
        table1["status"] = "no data were found"
        table1["data"] = {{}}
    end

    server.setBuffer()

    local success, jsonstr = server.table_to_json(table1)
    if not success then
        server.sendStatus(500)
        server.log(jsonstr, server.loglevel.err)
        return false
    end

    server.sendHeader('Content-type', 'application/json')
    server.sendStatus(200)
    server.print(jsonstr)
end

function getFileResource()
    local id = string.match(uri, "%d+")
    local data = readRes(id)

    -- Data does not exist in the database
    if (data == nil) then
        server.sendHeader('Content-type', 'application/json')
        server.sendStatus(404)
        return
    end

    local serverFilename = data["filename"]
    local mimetype = data["mimetype"]

    if (serverFilename == nil) or (mimetype == nil) then
        server.sendHeader('Content-type', 'application/json')
        server.sendStatus(500)
        print("no file attached to resource")
        return
    end

    local newFileName, errMsg = generateFileName(data)

    if (errMsg ~= nil) then
        server.sendHeader('Content-type', 'application/json')
        server.sendStatus(errMsg)
        return
    end

    local fileContent = readFile(serverFilename)

    if (fileContent == nil) then
        server.sendHeader('Content-type', 'application/json')
        server.sendStatus(500)
        print("failed to read file")
        return
    end

    server.setBuffer()
    server.sendHeader('Access-Control-Expose-Headers','Content-Disposition');
    server.sendHeader('Content-type', mimetype)
    server.sendHeader('Content-Disposition', "attachment; filename=" .. newFileName)
    server.sendStatus(200)
    server.print(fileContent)
    return
end

-- Checking of the url and appling the appropriate function
baseURL = "^/api"
uri = server.uri

routes = {}
routes[baseURL .. "/resources$"] = getResources
routes[baseURL .. "/resources/%d+$"] = getResource
routes[baseURL .. "/resources/%d+/file$"] = getFileResource

for route, func in pairs(routes) do
    if (string.match(uri, route) ~= nil) then
        func()
        return
    end
end

-- Fails if no url matches
server.sendStatus(404)