-- Pins the secure-by-default cookie posture: no http_only/same_site options
-- are passed, so the rendered Set-Cookie headers must still carry
-- HttpOnly and SameSite=Lax, and both cookies must survive independently.
server.sendCookie("a", "1")
server.sendCookie("b", "2")
server.print("COOKIES_OK")
