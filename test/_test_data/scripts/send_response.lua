-------------------------------------------------------------------------------
-- String constants to be used in error messages
-------------------------------------------------------------------------------
MIMETYPES_INCONSISTENCY = "Submitted mimetypes and/or file extension are inconsistent"

FILE_NOT_READBLE = "Submitted file path could not be read: "

PARAMETERS_INCORRECT = "Parameters not set correctly"
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- This function is called from the route when an error occurred.
-- Parameters:
--     'status' (number):  HTTP status code to returned to the client
--     'msg'    (string):  error message describing the problem that occurred
--
-- Returns:
--    an unsuccessful HTTP response containing a JSON string with the member 'message'
-------------------------------------------------------------------------------
function send_error(status, msg)

    if type(status) == "number" and status ~= 200 and type(msg) == "string" then

        result = {
            message = msg
        }

        http_status = status

    else

        result = {
            message = "Unknown error. Please report this as a possible bug in a Sipi route."
        }

        http_status = 500

    end

    local success, errormsg = server.sendHeader("Content-Type", "application/json")
    if not success then
        print(errormsg)
    end
    server.sendStatus(http_status)
    local success, jsonstr = server.table_to_json(result)

    local success, errmsg = server.print(jsonstr)
    if not success then
        print(errormsg)
    end

end
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- This function is called from the route when the request could
-- be handled successfully.
--
-- Parameters:
--     'result' (table):  message to be returned to the client.
--
-- Returns:
--    a JSON string that represents the data contained in the table 'result'.
-------------------------------------------------------------------------------
function send_success(result)
    if type(result) == "table" then
        local success, errormsg = server.sendHeader("Content-Type", "application/json")
        if not success then
            print(">>>1 ", errormsg)
        end
        local success, jsonstr = server.table_to_json(result)
        if not success then
            print(">>>2 ", jsonstr)
            send_error(500, "Couldn't create json string!")
            return
        end
        local success, errormsg = server.print(jsonstr)
        if not success then
            print(">>>3 ", errormsg)
        end
    else
        send_error(500, "scripts/send_response.lua:send_success: Expected the param 'result' to be of type table, but " .. type(result) .. " given")
    end
end
