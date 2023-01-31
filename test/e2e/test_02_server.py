# Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
# Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
# This file is part of Sipi.
# Sipi is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Sipi is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Additional permission under GNU AGPL version 3 section 7:
# If you modify this Program, or any covered work, by linking or combining
# it with Kakadu (or a modified version of that library) or Adobe ICC Color
# Profiles (or a modified version of that library) or both, containing parts
# covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
# or both, the licensors of this Program grant you additional permission
# to convey the resulting work.
# See the GNU Affero General Public License for more details.
# You should have received a copy of the GNU Affero General Public
# License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
import pprint

import pytest
from pathlib import Path
import os
import os.path
import time
import datetime
import sys


# Tests basic functionality of the Sipi server.


class TestServer:
    component = "The Sipi server"

    def test_sipi_starts(self, manager):
        """start"""
        assert manager.sipi_is_running()

    def test_sipi_log_output(self, manager):
        """add routes"""
        assert "Added route" in manager.get_sipi_output()

    def test_lua_functions(self, manager):
        """call C++ functions from Lua scripts"""
        manager.expect_status_code("/test_functions", 200)

    def test_clean_temp_dir(self, manager):
        """remove old temporary files"""
        temp_dir = manager.sipi_working_dir + "/images/tmp"
        os.makedirs(temp_dir, exist_ok=True)

        file_to_leave = Path(temp_dir + "/test_ok.jp2")
        file_to_leave.touch()

        file_to_delete = Path(temp_dir + "/test_old.jp2")
        file_to_delete.touch()
        date = datetime.datetime(year=2016, month=1, day=1)
        mod_time = time.mktime(date.timetuple())
        os.utime(file_to_delete, (mod_time, mod_time))

        manager.expect_status_code("/test_clean_temp_dir", 200)

        assert file_to_leave.exists()
        assert not file_to_delete.exists()

    def test_lua_scripts(self, manager):
        """call Lua functions for mediatype handling"""
        manager.expect_status_code("/test_mediatype", 200)

    def test_lua_mimetype(self, manager):
        """call Lua functions for getting mimetype"""
        manager.expect_status_code("/test_mimetype_func", 200)

    def test_knora_session_parsing(self, manager):
        """call Lua function that gets the Knora session id from the cookie header sent to Sipi"""
        manager.expect_status_code("/test_knora_session_cookie", 200)

    def test_file_bytes(self, manager):
        """return an unmodified JPG file"""
        manager.compare_server_bytes(
            "/knora/Leaves.jpg/full/max/0/default.jpg", manager.data_dir_path("knora/Leaves.jpg"))

    def test_restrict(self, manager):
        """return a restricted image in a smaller size"""
        image_info = manager.get_image_info(
            "/knora/RestrictLeaves.jpg/full/max/0/default.jpg")

        page_geometry = [line.strip().split()[-1] for line in image_info.splitlines()
                         if line.strip().startswith("Page geometry:")][0]
        assert page_geometry == "128x128+0+0"

    def test_deny(self, manager):
        """return 401 Unauthorized if the user does not have permission to see the image"""
        manager.expect_status_code(
            "/knora/DenyLeaves.jpg/full/max/0/default.jpg", 401)

    def test_iiifurl_parsing(self, manager):
        """Return 400 for invalid IIIF URL's"""
        manager.expect_status_code("/unit//lena512.jp2", 400)
        manager.expect_status_code("/unit/lena512.jp2/max/0/default.jpg", 400)
        manager.expect_status_code(
            "/unit/lena512.jp2/full/max/default.jpg", 400)
        manager.expect_status_code(
            "/unit/lena512.jp2/full/max/!/default.jpg", 400)
        manager.expect_status_code("/unit/lena512.jp2/full/max/0/jpg", 400)

    def test_read_write(self, manager):
        """read an image file, convert it to JPEG2000 and write it"""

        expected_result = {
            "status": 0,
            "message": "OK"
        }

        response_json = manager.get_json("/read_write_lua")

        assert response_json == expected_result

    def test_jpg_with_comment(selfself, manager):
        """process an uploaded jpeg file with comment block properly"""

        response_json = manager.post_file(
            "/api/upload", manager.data_dir_path("unit/HasCommentBlock.JPG"), "image/jpeg")
        filename = response_json["filename"]
        response_json = manager.get_json(
            "/unit/{}/knora.json".format(filename))

        expected_result = {
            "@context": "http://sipi.io/api/file/3/context.json",
            "id": "http://127.0.0.1:1024/unit/{}".format(filename),
            "width": 373,
            "height": 496,
            "internalMimeType": "image/jp2",
            "originalMimeType": "image/jpeg",
            "originalFilename": "HasCommentBlock.JPG"
        }

        assert response_json == expected_result

    def test_mimeconsistency(self, manager):
        """upload any file and check mimetype consistency"""

        testdata = [
            {
                "filepath": "knora/Leaves.jpg",
                "mimetype": "image/jpeg",
                "expected_result": {
                    "consistency": True,
                    "origname": "Leaves.jpg"
                }
            },
            {
                "filepath": "knora/Leaves8NotJpeg.jpg",
                "mimetype": "image/jpeg",
                "expected_result": {
                    "consistency": False,
                    "origname": "Leaves8NotJpeg.jpg"
                }
            },
            {
                "filepath": "knora/csv_test.csv",
                "mimetype": "text/csv",
                "expected_result": {
                    "consistency": True,
                    "origname": "csv_test.csv"
                }
            },
            {
                "filepath": "knora/csv_test-2.csv",
                "mimetype": "text/csv",
                "expected_result": {
                    "consistency": True,
                    "origname": "csv_test-2.csv"
                },
            },
            {
                "filepath": "knora/hello.resource.xml",
                "mimetype": "application/xml",
                "expected_result": {
                    "consistency": True,
                    "origname": "hello.resource.xml"
                }
            },
            {
                "filepath": "knora/hello.resource.xml",
                "mimetype": "text/xml",
                "expected_result": {
                    "consistency": True,
                    "origname": "hello.resource.xml"
                }
            }
        ]

        for test in testdata:
            response_json = manager.post_file(
                "/api/mimetest", manager.data_dir_path(test["filepath"]), test["mimetype"])
            assert response_json == test["expected_result"]

    def test_thumbnail(self, manager):
        """accept a POST request to create a thumbnail with Content-Type: multipart/form-data"""
        response_json = manager.post_file(
            "/make_thumbnail", manager.data_dir_path("knora/Leaves.jpg"), "image/jpeg")
        filename = response_json["filename"]
        manager.expect_status_code(
            "/thumbs/{}.jpg/full/max/0/default.jpg".format(filename), 200)

        # given the temporary filename, create the file
        params = {
            "filename": filename,
            "originalfilename": "Leaves.jpg",
            "originalmimetype": "image/jpeg"
        }

        response_json2 = manager.post_request("/convert_from_file", params)
        filename_full = response_json2["filename_full"]
        filename_thumb = response_json2["filename_thumb"]

        manager.expect_status_code(
            "/knora/{}/full/max/0/default.jpg".format(filename_full), 200)
        manager.expect_status_code(
            "/knora/{}/full/max/0/default.jpg".format(filename_thumb), 200)

    def test_image_conversion(self, manager):
        """ convert and store an image file"""

        params = {
            "originalfilename": "Leaves.jpg",
            "originalmimetype": "image/jpeg",
            "source": manager.data_dir_path("knora/Leaves.jpg")
        }

        response_json = manager.post_request("/convert_from_binaries", params)

        filename_full = response_json["filename_full"]
        filename_thumb = response_json["filename_thumb"]

        manager.expect_status_code(
            "/knora/{}/full/max/0/default.jpg".format(filename_full), 200)
        manager.expect_status_code(
            "/knora/{}/full/max/0/default.jpg".format(filename_thumb), 200)

    def test_knora_info_validation(self, manager):
        """return a valid knora.json response"""

        time.sleep(3)

        testdata = [
            {
                "filepath": "unit/lena512.tif",
                "mimetype": "image/tiff",
                "expected_result": {
                    "@context": "http://sipi.io/api/file/3/context.json",
                    "id": "http://127.0.0.1:1024/unit/",
                    "width": 512,
                    "height": 512,
                    "originalFilename": "lena512.tif",
                    "originalMimeType": "image/tiff",
                    "internalMimeType": "image/jp2"
                }
            }, {
                "filepath": "unit/test.csv",
                "mimetype": "text/csv",
                "expected_result": {
                    "@context": "http://sipi.io/api/file/3/context.json",
                    "id": "http://127.0.0.1:1024/unit/",
                    "originalFilename": "test.csv",
                    "internalMimeType": "text/csv",
                    "checksumDerivative": "228beaf29a9cdc864f777107c8f1a7bf9243d4140a844b7117a81d7d91fa6e37",
                    "checksumOriginal": "228beaf29a9cdc864f777107c8f1a7bf9243d4140a844b7117a81d7d91fa6e37",
                    "fileSize": 39697
                }
            }
        ]

        for test in testdata:
            response_json = manager.post_file(
                "/api/upload", manager.data_dir_path(test["filepath"]), test["mimetype"])
            filename = response_json["filename"]
            if test["mimetype"] == "image/tiff":
                manager.expect_status_code(
                    "/unit/{}/full/max/0/default.jpg".format(filename), 200)
            else:
                manager.expect_status_code("/unit/{}".format(filename), 200)
            response_json = manager.get_json(
                "/unit/{}/knora.json".format(filename))
            expected_result = test["expected_result"]
            expected_result["id"] += filename
            assert response_json == expected_result

        # expected_result = {
        #    "width": 512,
        #    "height": 512,
        #    "originalFilename": "lena512.tif",
        #    "originalMimeType": "image/tiff",
        #    "internalMimeType": "image/jpx"
        # }

        # response_json = manager.post_file("/api/upload", manager.data_dir_path("unit/lena512.tif"), "image/tiff")
        # filename = response_json["filename"]
        # manager.expect_status_code("/unit/{}/full/max/0/default.jpg".format(filename), 200)

        # response_json = manager.get_json("/unit/{}/knora.json".format(filename))

        # assert response_json == expected_result

    def test_json_info_validation(self, manager):
        """pass the info.json request tests"""

        expected_result = {
            '@context': 'http://iiif.io/api/image/3/context.json',
            'id': 'http://127.0.0.1:1024/unit/_lena512.jp2',
            'type': 'ImageService3',
            'protocol': 'http://iiif.io/api/image',
            'profile': 'level2',
            'width': 512,
            'height': 512,
            'sizes': [
                {'width': 256, 'height': 256},
                {'width': 128, 'height': 128}
            ],
            'tiles': [{'width': 512, 'height': 512, 'scaleFactors': [1, 2, 3, 4]}],
            'extraFormats': ['tif', 'jp2'],
            'preferredFormats': ['jpg', 'tif', 'jp2', 'png'],
            'extraFeatures': [
                'baseUriRedirect',
                'canonicalLinkHeader',
                'cors',
                'jsonldMediaType',
                'mirroring',
                'profileLinkHeader',
                'regionByPct',
                'regionByPx',
                'regionSquare',
                'rotationArbitrary',
                'rotationBy90s',
                'sizeByConfinedWh',
                'sizeByH',
                'sizeByPct',
                'sizeByW',
                'sizeByWh',
                'sizeUpscaling'
            ]
        }

        response_json = manager.post_file(
            "/api/upload", manager.data_dir_path("unit/lena512.tif"), "image/tiff")
        filename = response_json["filename"]

        manager.expect_status_code(
            "/unit/{}/full/max/0/default.jpg".format(filename), 200)

        response_json = manager.get_json("/unit/{}/info.json".format(filename))
        expected_result["id"] = "http://127.0.0.1:1024/unit/{}".format(
            filename)
        assert response_json == expected_result

        # response_json = manager.get_json("/unit/{}/info.json".format(filename), use_ssl=True)
        # expected_result["id"] = "https://127.0.0.1:1024/unit/{}".format(filename)
        # assert response_json == expected_result

    def test_knora_json_for_video(self, manager):
        """pass the knora.json request for video"""

        expected_result = {
            "@context": "http://sipi.io/api/file/3/context.json",
            "id": "http://127.0.0.1:1024/unit/8pdET49BfoJ-EeRcIbgcLch.mp4",
            "checksumOriginal": "19cc4bccad39c89cc44936ef69565bb933d41a065fd59d666d58e5ef344e8149",
            "checksumDerivative": "19cc4bccad39c89cc44936ef69565bb933d41a065fd59d666d58e5ef344e8149",
            "internalMimeType": "video/mp4",
            "fileSize": 475205,
            "originalFilename": "Dummy.mp4",
            "duration": 4.7000000000000002,
            "fps": 30,
            "height": 240,
            "width": 320
        }

        response_json = manager.get_json(
            "/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json")
        assert response_json == expected_result

    def test_handling_of_missing_sidecar_file_for_video(self, manager):
        """correctly handle missing sidecar file for video"""

        expected_result = {
            "@context": "http://sipi.io/api/file/3/context.json",
            "id": "http://127.0.0.1:1024/unit/has-missing-sidecar-file.mp4",
            "internalMimeType": "video/mp4",
            "fileSize": 475205,
        }

        response_json = manager.get_json(
            "/unit/has-missing-sidecar-file.mp4/knora.json")
        assert response_json == expected_result

    def test_sqlite_api(self, manager):
        """Test sqlite API"""
        expected_result = {
            "result": {
                "512": "Dies ist ein erster Text",
                "1024": "Un der zweite Streich folgt sogleich"
            }
        }
        json_result = manager.get_json("/sqlite")
        assert json_result == expected_result

    def test_iiif_auth_api(self, manager):
        """Test the IIIF Auth Api that returns HTTP code 401 and a info.json"""
        expected_result = {
            "@context": "http://iiif.io/api/image/2/context.json",
            "@id": "http://127.0.0.1:1024/auth/lena512.jp2",
            "protocol": "http://iiif.io/api/image",
            "service": {
                "@context": "http://iiif.io/api/auth/1/context.json",
                "@id": "https://localhost/iiif-cookie.html",
                "profile": "http://iiif.io/api/auth/1/login",
                "description": "This Example requires a demo login!",
                "label": "Login to SIPI",
                "failureHeader": "Authentication Failed",
                "failureDescription": "<a href=\"http://example.org/policy\">Access Policy</a>",
                "confirmLabel": "Login to SIPI",
                "header": "Please Log In",
                "service": [
                    {
                        "@id": "https://localhost/iiif-token.php",
                        "profile": "http://iiif.io/api/auth/1/token"
                    }
                ]
            },
            "width": 512,
            "height": 512,
            "sizes": [
                {
                    "width": 256,
                    "height": 256
                },
                {
                    "width": 128,
                    "height": 128
                }
            ],
            "profile": [
                "http://iiif.io/api/image/2/level2.json",
                {
                    "formats": [
                        "tif",
                        "jpg",
                        "png",
                        "jp2",
                    ],
                    "qualities": [
                        "color",
                        "gray"
                    ],
                    "supports": [
                        "color",
                        "cors",
                        "mirroring",
                        "profileLinkHeader",
                        "regionByPct",
                        "regionByPx",
                        "rotationArbitrary",
                        "rotationBy90s",
                        "sizeAboveFull",
                        "sizeByWhListed",
                        "sizeByForcedWh",
                        "sizeByH",
                        "sizeByPct",
                        "sizeByW",
                        "sizeByWh"
                    ]
                }
            ]
        }

        expected_result = {
            '@context': 'http://iiif.io/api/image/3/context.json',
            'id': 'http://127.0.0.1:1024/auth/lena512.jp2',
            'type': 'ImageService3',
            'protocol': 'http://iiif.io/api/image',
            'profile': 'level2',
            'service': [
                {
                    '@context': 'http://iiif.io/api/auth/1/context.json',
                    '@id': 'https://localhost/iiif-cookie.html',
                    'profile': 'http://iiif.io/api/auth/1/login',
                    'header': 'Please Log In',
                    'failureDescription': '<a href="http://example.org/policy">Access Policy</a>',
                    'confirmLabel': 'Login to SIPI',
                    'failureHeader': 'Authentication Failed',
                    'description': 'This Example requires a demo login!',
                    'label': 'Login to SIPI',
                    'service': [
                        {
                            '@id': 'https://localhost/iiif-token.php',
                            'profile': 'http://iiif.io/api/auth/1/token'
                        }
                    ]
                }
            ],
            'width': 512,
            'height': 512,
            'sizes': [
                {'width': 256, 'height': 256},
                {'width': 128, 'height': 128}
            ],
            'tiles': [{
                'width': 512,
                'height': 512,
                'scaleFactors': [1, 2, 3, 4, 5, 6, 7]
            }],
            'extraFormats': ['tif', 'jp2'],
            'preferredFormats': ['jpg', 'tif', 'jp2', 'png'],
            'extraFeatures': [
                'baseUriRedirect',
                'canonicalLinkHeader',
                'cors',
                'jsonldMediaType',
                'mirroring',
                'profileLinkHeader',
                'regionByPct',
                'regionByPx',
                'regionSquare',
                'rotationArbitrary',
                'rotationBy90s',
                'sizeByConfinedWh',
                'sizeByH',
                'sizeByPct',
                'sizeByW',
                'sizeByWh',
                'sizeUpscaling']}

        json_result = manager.get_auth_json("/auth/lena512.jp2/info.json")

        assert json_result == expected_result

    def test_orientation_topleft(self, manager):
        """Test rectifying the image orientation to topleft"""
        response_json = manager.post_file(
            "/api/upload", manager.data_dir_path("unit/image_orientation.jpg"), "image/jpeg")
        filename = response_json["filename"]
        manager.compare_server_images(
            "/unit/{}/full/max/0/default.tif".format(filename), manager.data_dir_path("unit/image_orientation.tif"))

    def test_4bit_palette_png(self, manager):
        """Test reading 4 bit palette png with alpha channel"""
        response_json = manager.post_file(
            "/api/upload", manager.data_dir_path("unit/mario.png"), "image/png")
        filename = response_json["filename"]
        manager.compare_server_images(
            "/unit/{}/full/max/0/default.tif".format(filename), manager.data_dir_path("unit/mario.tif"))


    def test_upscaling_server(self, manager):
        """Test upscaling of an image"""
        manager.compare_server_images(
            "/unit/lena512.jp2/full/^1000,/0/default.tif", manager.data_dir_path("unit/lena512_upscaled.tif"))

    def test_concurrency(self, manager):
        """handle many concurrent requests for different URLs (this may take a while, please be patient)"""

        # The command-line arguments we want to pass to ab for each process.

        filename = "load_test.jpx"

        ab_processes = [
            {
                "concurrent_requests": 5,
                "total_requests": 10,
                "url_path": "/knora/{}/full/max/0/default.jpg".format(filename)
            },
            {
                "concurrent_requests": 5,
                "total_requests": 10,
                "url_path": "/knora/{}/full/pct:50/0/default.jpg".format(filename)
            },
            {
                "concurrent_requests": 5,
                "total_requests": 10,
                "url_path": "/knora/{}/full/max/90/default.jpg".format(filename)
            },
            {
                "concurrent_requests": 25,
                "total_requests": 30,
                "url_path": "/knora/{}/pct:10,10,40,40/max/0/default.jpg".format(filename)
            },
            {
                "concurrent_requests": 25,
                "total_requests": 30,
                "url_path": "/knora/{}/pct:10,10,50,30/max/180/default.jpg".format(filename)
            }
        ]

        # Start all the ab processes.

        for process_info in ab_processes:
            process_info["process"] = manager.run_ab(
                process_info["concurrent_requests"], process_info["total_requests"], 300, process_info["url_path"])

        # Wait for all the processes to terminate, and get their return codes and output.

        for process_info in ab_processes:
            process = process_info["process"]

            # stderr has been redirected to stdout and is therefore None
            # do not use wait() because of the danger of a deadlock: https://docs.python.org/3.5/library/subprocess.html#subprocess.Popen.wait
            stdout, stderr = process.communicate(timeout=300)

            process_info["returncode"] = process.returncode
            process_info["stdout"] = stdout

        # Check whether they all succeeded.

        bad_result = False
        failure_results = "\n"

        for process_info in ab_processes:
            # Strip out non-ASCII characters, because for some reason ab includes an invalid 0xff
            stdout_str = process_info["stdout"].decode("ascii", "ignore")
            non_2xx_responses_lines = [line.strip().split(
            )[-1] for line in stdout_str.splitlines() if line.strip().startswith("Non-2xx responses:")]

            if len(non_2xx_responses_lines) > 0:
                bad_result = True
                failure_results += "Sipi returned a non-2xx response for URL path {}, returncode {}, and output:\n{}".format(
                    process_info["url_path"], process_info["returncode"], stdout_str)

            if process_info["returncode"] != 0:
                bad_result = True
                failure_results += "Failed ab command with URL path {}, returncode {}, and output:\n{}".format(
                    process_info["url_path"], process_info["returncode"], stdout_str)

        if bad_result:
            failure_results += "\nWrote Sipi log file " + manager.sipi_log_file

        assert not bad_result, failure_results

    def test_orientation(self, manager):
        """convert image to always have top-left orientation"""
        manager.expect_status_code("/test_orientation", 200)
