# SQLite API endpoint
GET {{host}}/sqlite
HTTP 200
[Asserts]
jsonpath "$.result.512" == "Dies ist ein erster Text"
jsonpath "$.result.1024" == "Un der zweite Streich folgt sogleich"
