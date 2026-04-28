# IIIF Image API 3.0 info.json — image-information document.
# Asserts the IIIF context, conformance @id and protocol, and that the
# image dimensions match the lena512.jp2 fixture (512x512).
GET {{host}}/unit/lena512.jp2/info.json
HTTP 200
[Asserts]
header "Content-Type" startsWith "application/ld+json"
jsonpath "$['@context']" contains "http://iiif.io/api/image/3"
jsonpath "$.protocol" == "http://iiif.io/api/image"
jsonpath "$.type" == "ImageService3"
jsonpath "$.width" == 512
jsonpath "$.height" == 512
jsonpath "$.profile" exists
