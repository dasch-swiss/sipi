
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
-- it with Kakadu (or a modified version of that library) or Adobe ICC Color
-- Profiles (or a modified version of that library) or both, containing parts
-- covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
-- or both, the licensors of this Program grant you additional permission
-- to convey the resulting work.
-- You should have received a copy of the GNU Affero General Public
-- License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
--

-------------------------------------------------------------------------------
-- This function should be used to authorize a webpage using embeded lua. If
-- there is no Cookie containing a valid JWT or a "Authorization: BEARER" header,
-- a standard HTTP Basic authorization is performed. If successfull, a JWT is
-- created which is returned by the function. In addition, a Cookie names "sipi"
-- is being created that holds the token.
--
-- @param issuer The issuer of the JWT
-- @param audience The audience targeted by the JWT
-- @param username The username that is used for authentification
-- @param password The password that is used. This parameter can be omitted in case a JWT is expected
-- @return true and the JWT token, or false
--
-- The usage is as follows:
--
--    <lua>
--    success, token = authorize_page('username', 'password', 'issuer', 'audience')
--    if not success  then
--       return
--    end
--    </lua>
-------------------------------------------------------------------------------
function authorize_page(issuer, audience, username, password)
    --
    -- Check if we can have a secure connection
    --
    if server.has_openssl and not server.secure then
        --
        -- redirect to secure port
        --
        local pos = string.find(server.host, ':', 1, true)
        local host
        if (pos ~= nil) then
            host = string.sub(server.host, 1, pos - 1)
        else
            host = server.host
        end
        if config.sslport ~= 443 then
            host = host .. ':' .. config.sslport
        end
        server.sendHeader('location', 'https://'  .. host .. server.uri)
        server.sendStatus(301)
        return false
    end

    if server.secure then
        protocol = 'https://'
    else
        protocol = 'http://'
    end

    --
    -- authentication
    --
    local token
    if server.has_openssl then
        --
        -- first we check if we get a cookie with a valid JWT
        --
        if server.cookies['sipi'] then
            token = server.cookies['sipi']
            local success, jwt = server.decode_jwt(token)
            if not success then
                server.sendStatus(500)
                server.log(jwt, server.loglevel.LOG_ERR)
                return false
            end
            if (jwt.iss ~= issuer) or (jwt.aud ~= audience) or (jwt.user ~= username) then
                server.sendStatus(401)
                server.sendHeader('WWW-Authenticate', 'Basic realm="SIPI"')
                return false
            end
            server.log("Accessing protected page...", server.loglevel.LOG_ERR)
        else
            --
            -- otherwise we require an authentification header
            --
            local success, auth = server.requireAuth()

            if not success then
                server.sendStatus(500)
                server.log(auth, server.loglevel.LOG_ERR)
                return false
            end

            if auth.status == 'BASIC' then
                --
                -- everything OK, let's create the token for further calls and ad it to a cookie
                --
                if auth.username == username and auth.password == password then
                    tokendata = {
                        iss = issuer,
                        aud = audience,
                        user = username
                    }
                    success, token = server.generate_jwt(tokendata)
                    if not success then
                        server.sendStatus(500)
                        server.log(token, server.loglevel.LOG_ERR)
                        return false
                    end
                    server.sendCookie('sipi', token, {path = '/', expires = 3600})
                else
                    server.sendStatus(401)
                    server.sendHeader('WWW-Authenticate', 'Basic realm="SIPI"')
                    server.print("Wrong credentials!")
                    return false
                end
            elseif auth.status == 'BEARER' then
                local success, jwt = server.decode_jwt(auth.token)
                if not success then
                    server.sendStatus(500)
                    server.log(jwt, server.loglevel.LOG_ERR)
                    return false
                end
                if (jwt.iss ~= issuer) or (jwt.aud ~= audience) or (jwt.user ~= username) then
                    server.sendStatus(401)
                    server.sendHeader('WWW-Authenticate', 'Basic realm="SIPI"')
                    return false
                end
            elseif auth.status == 'NOAUTH' then
                server.setBuffer()
                server.sendStatus(401);
                server.sendHeader('WWW-Authenticate', 'Basic realm="SIPI"')
                return false
            else
                server.status(401)
                server.sendHeader('WWW-Authenticate', 'Basic realm="SIPI"')
                return false
            end
        end
    else
        --
        -- no openssl, we just use insecure basic authentification
        -- ATTENTION: This is a severe security hole and should only
        -- used on private networks You can trust!!
        --
        auth = server.requireAuth()
        if auth.username ~= username or auth.password ~= password then
            server.sendStatus(401)
            server.sendHeader('WWW-Authenticate', 'Basic realm="SIPI"')
            server.print("Wrong credentials!")
            return false
        end
    end
    return true, token
end
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- This function is used to authorize the sipi api
-- @param issuer The issuer of the JWT
-- @param audience The audience targeted by the JWT
-- @param username The username that is used for authentification
--
-- The usage is as follows:
--
--    <lua>
--    if not authorize_api(issuer, audience, username) then
--       return
--    end
--    </lua>
-------------------------------------------------------------------------------
function authorize_api(issuer, audience, username)
    local success, auth = server.requireAuth()
    if not success then
        server.sendStatus(500)
        server.log(auth, server.loglevel.LOG_ERR)
        return false
    end

    if (auth.status ~= 'BEARER') then
        server.sendStatus(401)
        server.sendHeader('WWW-Authenticate', 'Bearer')
        server.print("Wrong credentials!")
        return false
    end

    local success, jwt = server.decode_jwt(auth.token)
    if not success then
        server.sendStatus(500)
        server.log(jwt, server.loglevel.LOG_ERR)
        return false
    end
    if (jwt.iss ~= issuer) or (jwt.aud ~= audience) or (jwt.user ~= username) then
        server.sendStatus(401)
        server.sendHeader('WWW-Authenticate', 'Bearer')
        return false
    end
    return true
end
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- This function is being called from sipi before the file is served
--
-- @param prefix This is the prefix that is given on the IIIF url
-- @param identifier the identifier for the image
-- @param cookie The cookie that may be present
--
-- @return permission, filepath
--     permission:
--        The permission can be
--        1) either the string 'allow' or 'deny'
--        2) a table with the following structure. 'type' is required, the other fields depend on the type
--          {
--             type = 'allow' | 'login' | 'clickthrough' | 'kiosk' | 'external' | 'restrict' | 'deny',
--             cookieUrl = '<URL>',
--             tokenUrl = '<URL>',
--             logoutUrl = '<URL>',  -- optional
--             confirmLabel = '<string>', -- optional
--             description = '<string>',  -- optional
--             failureDescription = '<string>',  -- optional
--             failureHeader = '<string>',  -- optional
--             header = '<string>',  -- optional
--             watermark = '<path-to-wm-file>', -- for type = restrict only
--             size = '<iiif-size-string>' -- for type = restrict only
--          }
--         'allow' : the view is allowed with the given IIIF parameters
--         'login': cookie-url token-url [logout-url]: IIIF Auth API login profile
--         'clickthrough': required: cookie-url token-url: IIIF Auth API clickthrough profile
--         'kiosk': required: cookie-url token-url: IIIF Auth API kiosk profile
--         'external': required: token-url: IIIF Auth API external profile
--         'deny' : no access!
--    filepath: server-path where the master file is located
-------------------------------------------------------------------------------
function pre_flight(prefix,identifier,cookie)
    local success, result

    if prefix == 'imgrep' then
        local url = 'http://127.0.0.1/salsah/api/iiif/' .. identifier

        if cookie then
            local cookie_header = { Cookie = cookie }
            success, result = server.http('GET', url, cookie_header)
        else
            success, result = server.http('GET', url)
        end

        if success then
            success, response_json = server.json_to_table(result.body)
            if success then
                local access = 'deny'
                if response_json.rights == 'ACCESS' then
                    access = 'allow'
                elseif  response_json.rights == 'RESTRICTED' then
                    if response_json.watermark then
                        access = 'restrict:watermark=' .. response_json.watermark
                    else
                        access = 'restrict'
                    end
                end
                local filepath = response_json.path
                server.log('access = ' .. access .. ' filepath = ' .. filepath, server.loglevel.LOG_ERR)
                return access, filepath
            else
                server.log("Server.http() failed: " .. response_json, server.loglevel.LOG_ERR)
                return 'deny'
            end
        else
            server.log("Server.http() failed: " .. result, server.loglevel.LOG_ERR)
            return 'deny'
        end
    elseif string.find(identifier, "restricted") then
        local filepath = config.imgroot .. '/' .. identifier
        local settings_for_sipi = {}
        settings_for_sipi['type'] = "restrict"
        settings_for_sipi['size'] = "!500,500"
        return settings_for_sipi, filepath
    elseif string.find(identifier, "watermarked") then
        local filepath = config.imgroot .. '/' .. identifier
        local settings_for_sipi = {}
        settings_for_sipi['type'] = "restrict"
        settings_for_sipi['watermark'] = config.imgroot .. '/' .. "watermark.tif"
        return settings_for_sipi, filepath
    elseif string.find(identifier, "restricted_watermarked") then
        local filepath = config.imgroot .. '/' .. identifier
        local settings_for_sipi = {}
        settings_for_sipi['type'] = "restrict"
        settings_for_sipi['size'] = "!500,500"
        settings_for_sipi['watermark'] = config.imgroot .. '/' .. "watermark.tif"
        return settings_for_sipi, filepath
    else
        if config.prefix_as_path then
            filepath = config.imgroot .. '/' .. prefix .. '/' .. identifier
        else
            filepath = config.imgroot .. '/' .. identifier
        end
    end

    --db = sqlite('db/test.db', 'RW')
    --qry = db << 'SELECT * FROM image'

    --
    -- Example of a sqlite3 query
    --
    --row = qry()
    --while (row) do
    --    print(row[0], ' -> ', row[1])
    --    row = qry()
    --end
    --qry = ~qry
    --db = ~db

    return 'allow', filepath

    --[[

        if config.prefix_as_path then
            filepath = config.imgroot .. '/' .. prefix .. '/' .. identifier
        else
            filepath = config.imgroot .. '/' .. identifier
        end

        if server.cookies['sipi-auth'] then
            print('preflight: IIIF cookie')
            access_info = server.cookies['sipi-auth']
            success, token_val = server.decode_jwt(access_info)
            if not success then
                server.sendStatus(500)
                server.log(token_val, server.loglevel.LOG_ERR)
                return false
            end
            if token_val['allow'] then
                return 'allow', filepath
            end
        elseif server.header['authorization'] then
            access_info = string.sub(server.header['authorization'], 8)
            success, token_val = server.decode_jwt(access_info)
            if not success then
                server.sendStatus(500)
                server.log(token_val, server.loglevel.LOG_ERR)
                return false
            end
            if token_val['allow'] then
                return 'allow', filepath
            end
        else
            return {
                type = 'login',
                cookieUrl = 'https://localhost/iiif-cookie.html',
                tokenUrl = 'https://localhost/iiif-token.php',
                confirmLabel =  'Login to SIPI',
                description = 'This Example requires a demo login!',
                failureDescription = '<a href="http://example.org/policy">Access Policy</a>',
                failureHeader = 'Authentication Failed',
                header = 'Please Log In',
                label = 'Login to SIPI',
            }, filepath
        end
    --]]
end
-------------------------------------------------------------------------------
