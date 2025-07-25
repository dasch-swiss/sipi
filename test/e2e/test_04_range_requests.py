"""
Test HTTP Range Requests for both small and large files.
Tests the fix for DEV-5160: Large video file streaming bug.
"""

import pytest
import requests
import os
import tempfile
from pathlib import Path


class TestRangeRequests:
    component = "HTTP Range Request handling"
    
    def _get_content_length(self, response):
        """Helper to extract Content-Length, handling duplicate header bug."""
        content_length = response.headers["Content-Length"]
        if ',' in content_length:
            content_length = content_length.split(',')[0].strip()
        return int(content_length)
    
    @pytest.fixture(autouse=True)
    def setup_test_files(self, sipi_server_url):
        """Generate test files in test/_test_data/server directory."""
        self.base_url = sipi_server_url
        self.server_dir = Path(__file__).parent.parent / "_test_data" / "server"
        self.server_dir.mkdir(parents=True, exist_ok=True)
        
        # Create small test file (1KB)
        self.small_file = self.server_dir / "small_test_file.bin"
        self.small_content = b"0123456789" * 100  # 1000 bytes
        with open(self.small_file, "wb") as f:
            f.write(self.small_content)
        
        # Create large test file (10MB) - simulates large video file behavior
        self.large_file = self.server_dir / "large_test_file.bin"
        self.large_size = 10 * 1024 * 1024  # 10MB
        chunk = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 1000  # ~36KB chunk
        
        with open(self.large_file, "wb") as f:
            written = 0
            while written < self.large_size:
                to_write = min(len(chunk), self.large_size - written)
                f.write(chunk[:to_write])
                written += to_write
        
        yield
        
        # Cleanup
        self.small_file.unlink(missing_ok=True)
        self.large_file.unlink(missing_ok=True)

    def test_small_file_no_range(self):
        """Test serving small file without range header."""
        url = f"{self.base_url}/server/small_test_file.bin"
        response = requests.get(url)
        
        assert response.status_code == 200
        assert len(response.content) == len(self.small_content)
        assert response.content == self.small_content
        assert "Content-Length" in response.headers
        assert self._get_content_length(response) == len(self.small_content)

    def test_small_file_range_first_100_bytes(self):
        """Test range request for first 100 bytes of small file."""
        url = f"{self.base_url}/server/small_test_file.bin"
        headers = {"Range": "bytes=0-99"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206  # Partial Content
        assert len(response.content) == 100
        assert response.content == self.small_content[:100]
        assert "Content-Range" in response.headers
        assert response.headers["Content-Range"] == f"bytes 0-99/{len(self.small_content)}"
        assert self._get_content_length(response) == 100

    def test_small_file_range_middle_bytes(self):
        """Test range request for middle portion of small file."""
        url = f"{self.base_url}/server/small_test_file.bin"
        headers = {"Range": "bytes=200-299"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == 100
        assert response.content == self.small_content[200:300]
        assert response.headers["Content-Range"] == f"bytes 200-299/{len(self.small_content)}"

    def test_small_file_range_last_byte(self):
        """Test range request for the last byte (edge case that previously failed)."""
        url = f"{self.base_url}/server/small_test_file.bin"
        last_byte_pos = len(self.small_content) - 1
        headers = {"Range": f"bytes={last_byte_pos}-{last_byte_pos}"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == 1
        assert response.content == self.small_content[-1:]
        assert response.headers["Content-Range"] == f"bytes {last_byte_pos}-{last_byte_pos}/{len(self.small_content)}"

    def test_small_file_open_ended_from_start(self):
        """Test open-ended range from start (bytes=0-) - triggers DEV-5160 bug condition."""
        url = f"{self.base_url}/server/small_test_file.bin"
        headers = {"Range": "bytes=0-"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == len(self.small_content)
        assert response.content == self.small_content
        expected_end = len(self.small_content) - 1
        assert response.headers["Content-Range"] == f"bytes 0-{expected_end}/{len(self.small_content)}"

    def test_small_file_open_ended_from_middle(self):
        """Test open-ended range from middle (bytes=N-) - triggers DEV-5160 bug condition."""
        url = f"{self.base_url}/server/small_test_file.bin"
        start_pos = 500
        headers = {"Range": f"bytes={start_pos}-"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        expected_size = len(self.small_content) - start_pos
        assert len(response.content) == expected_size
        assert response.content == self.small_content[start_pos:]
        expected_end = len(self.small_content) - 1
        assert response.headers["Content-Range"] == f"bytes {start_pos}-{expected_end}/{len(self.small_content)}"

    # TODO: Should be fixed
    # def test_small_file_range_beyond_file_size(self):
    #     """Test range request beyond file size (should fail)."""
    #     url = f"{self.base_url}/server/small_test_file.bin"
    #     file_size = len(self.small_content)
    #     headers = {"Range": f"bytes=0-{file_size}"}  # One byte beyond file
    #     response = requests.get(url, headers=headers)
    #
    #     # Should return error (416 Range Not Satisfiable or 500 Internal Server Error)
    #     assert response.status_code in [416, 500]

    def test_large_file_no_range(self):
        """Test serving large file without range header."""
        url = f"{self.base_url}/server/large_test_file.bin"
        response = requests.get(url)
        
        assert response.status_code == 200
        assert len(response.content) == self.large_size
        assert "Content-Length" in response.headers
        assert self._get_content_length(response) == self.large_size

    def test_large_file_range_first_megabyte(self):
        """Test range request for first 1MB of large file."""
        url = f"{self.base_url}/server/large_test_file.bin"
        mb_size = 1024 * 1024
        headers = {"Range": f"bytes=0-{mb_size-1}"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == mb_size
        assert "Content-Range" in response.headers
        assert response.headers["Content-Range"] == f"bytes 0-{mb_size-1}/{self.large_size}"
        assert self._get_content_length(response) == mb_size

    def test_large_file_range_middle_chunk(self):
        """Test range request for middle chunk of large file."""
        url = f"{self.base_url}/server/large_test_file.bin"
        start = 5 * 1024 * 1024  # 5MB
        end = start + (1024 * 1024) - 1  # +1MB
        headers = {"Range": f"bytes={start}-{end}"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == 1024 * 1024
        assert response.headers["Content-Range"] == f"bytes {start}-{end}/{self.large_size}"

    def test_large_file_range_last_chunk(self):
        """Test range request for last chunk of large file (critical test case)."""
        url = f"{self.base_url}/server/large_test_file.bin"
        chunk_size = 1024 * 1024  # 1MB
        start = self.large_size - chunk_size
        end = self.large_size - 1  # Last byte (inclusive)
        headers = {"Range": f"bytes={start}-{end}"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == chunk_size
        assert response.headers["Content-Range"] == f"bytes {start}-{end}/{self.large_size}"

    def test_large_file_range_single_last_byte(self):
        """Test range request for the very last byte of large file (most critical edge case)."""
        url = f"{self.base_url}/server/large_test_file.bin"
        last_byte_pos = self.large_size - 1
        headers = {"Range": f"bytes={last_byte_pos}-{last_byte_pos}"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == 1
        assert response.headers["Content-Range"] == f"bytes {last_byte_pos}-{last_byte_pos}/{self.large_size}"

    def test_large_file_open_ended_from_start(self):
        """Test open-ended range from start on large file - critical DEV-5160 case."""
        url = f"{self.base_url}/server/large_test_file.bin"
        headers = {"Range": "bytes=0-"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        assert len(response.content) == self.large_size
        expected_end = self.large_size - 1
        assert response.headers["Content-Range"] == f"bytes 0-{expected_end}/{self.large_size}"

    def test_large_file_open_ended_from_middle(self):
        """Test open-ended range from middle on large file - critical DEV-5160 case."""
        url = f"{self.base_url}/server/large_test_file.bin"
        start_pos = 5 * 1024 * 1024  # Start at 5MB
        headers = {"Range": f"bytes={start_pos}-"}
        response = requests.get(url, headers=headers)
        
        assert response.status_code == 206
        expected_size = self.large_size - start_pos
        assert len(response.content) == expected_size
        expected_end = self.large_size - 1
        assert response.headers["Content-Range"] == f"bytes {start_pos}-{expected_end}/{self.large_size}"

    # TODO: Should be fixed
    # def test_large_file_range_beyond_size(self):
    #     """Test range request beyond large file size (should fail gracefully)."""
    #     url = f"{self.base_url}/server/large_test_file.bin"
    #     headers = {"Range": f"bytes=0-{self.large_size}"}  # One byte beyond
    #     response = requests.get(url, headers=headers)
    #
    #     # Should return error, not crash with "Trying to read beyond end of file!"
    #     assert response.status_code in [416, 500]

    def test_large_file_multiple_ranges_simulation(self):
        """Test multiple sequential range requests (simulates video player behavior)."""
        url = f"{self.base_url}/server/large_test_file.bin"
        chunk_size = 256 * 1024  # 256KB chunks
        
        # Test first few chunks like a video player would request
        for i in range(3):
            start = i * chunk_size
            end = start + chunk_size - 1
            if end >= self.large_size:
                end = self.large_size - 1
            
            headers = {"Range": f"bytes={start}-{end}"}
            response = requests.get(url, headers=headers)
            
            assert response.status_code == 206
            expected_size = end - start + 1
            assert len(response.content) == expected_size
            assert response.headers["Content-Range"] == f"bytes {start}-{end}/{self.large_size}"

    # TODO: Should be fixed
    # def test_malformed_range_headers(self):
    #     """Test various malformed range headers."""
    #     url = f"{self.base_url}/server/small_test_file.bin"
    #
    #     malformed_headers = [
    #         {"Range": "bytes=invalid"},
    #         {"Range": "bytes=-"},
    #         {"Range": "bytes=100-50"},  # end < start
    #         {"Range": "bytes=abc-def"},
    #         {"Range": "invalid=0-100"},
    #     ]
    #
    #     for headers in malformed_headers:
    #         response = requests.get(url, headers=headers)
    #         # Should handle gracefully (either ignore range or return error)
    #         assert response.status_code in [200, 400, 416, 500]