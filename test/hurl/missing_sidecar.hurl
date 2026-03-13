# Handle missing sidecar file gracefully
GET {{host}}/unit/has-missing-sidecar-file.mp4/knora.json
HTTP 200
[Asserts]
jsonpath "$.internalMimeType" == "video/mp4"
jsonpath "$.fileSize" == 475205
