# Direct file access for allowed file
GET {{host}}/unit/test.csv/file
HTTP 200

# Direct file access for denied file
GET {{host}}/unit/test2.csv/file
HTTP 401
