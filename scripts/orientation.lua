---
--- Generated by EmmyLua(https://github.com/EmmyLua)
--- Created by rosenth.
--- DateTime: 11.01.23 00:34
---
server.setBuffer()
server.sendHeader("Content-Type", "text/html")

local success, img = SipiImage.new(config.imgroot .. "/Anubis.jpg")
if not success then
    server.sendStatus(500)
    server.log(gaga, server.loglevel.LOG_ERR)
    return false
end

local status, ori1 = img:exif("Orientation")
status = img:topleft()
status, ori2 = img:exif("Orientation")

local outpath = config.imgroot .. "/Anubis.jp2"
img:write(outpath)

server.sendStatus(200)
server.print("RESULT1=" .. ori1 .. "\n")
server.print("RESULT2=" .. ori2 .. "\n")
return true
