# knora.json for a video file
GET {{host}}/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json
HTTP 200
[Asserts]
jsonpath "$.internalMimeType" == "video/mp4"
jsonpath "$.width" == 320
jsonpath "$.height" == 240
jsonpath "$.fps" == 30
jsonpath "$.originalFilename" == "Dummy.mp4"
