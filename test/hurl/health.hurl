# /health endpoint — used by Docker HEALTHCHECK and monitoring.
# Returns a JSON document with status, version, and uptime.
GET {{host}}/health
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$.status" == "ok"
jsonpath "$.version" exists
jsonpath "$.uptime_seconds" exists
jsonpath "$.uptime_seconds" >= 0
