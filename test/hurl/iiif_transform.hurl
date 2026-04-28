# Canonical IIIF Image API 3.0 transformation: full identifier serves a
# rendered image. Exercises the format-conversion contract end-to-end
# (JP2 decode → JPEG encode through libjpeg/Kakadu/lcms2/exiv2).
GET {{host}}/unit/lena512.jp2/full/max/0/default.jpg
HTTP 200
[Asserts]
header "Content-Type" == "image/jpeg"
bytes count > 1000

# Region/size/rotation/quality parameter parsing.
GET {{host}}/unit/lena512.jp2/0,0,256,256/128,/0/default.jpg
HTTP 200
[Asserts]
header "Content-Type" == "image/jpeg"
bytes count > 100

# Bare identifier redirects to canonical info.json (303 See Other).
GET {{host}}/unit/lena512.jp2
HTTP 303
[Asserts]
header "Location" endsWith "/unit/lena512.jp2/info.json"
