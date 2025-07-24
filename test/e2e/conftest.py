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
import configparser
import os
import shlex
import signal
import subprocess
import threading
import time
import requests
import tempfile
import filecmp
import shutil
import psutil
import re
import hashlib
import glob
import sys
import socket
import urllib.request
import urllib.error


def pytest_addoption(parser):
    parser.addoption(
        "--sipi-exec", action="store", default="notset", help="The absolut path to the sipi executable"
    )


@pytest.fixture(scope="session")
def manager(request):
    """Returns a SipiTestManager. Automatically starts Sipi and nginx before tests are run, and
    stops them afterwards."""
    manager = SipiTestManager(request.config.getoption('--sipi-exec'))
    manager.start_nginx()
    manager.start_sipi()
    yield manager
    manager.stop_sipi()
    manager.stop_nginx()
    manager.cleanup()


@pytest.fixture(scope="session")
def sipi_server_url(manager):
    """Returns the base URL for the SIPI server."""
    return manager.sipi_base_url


class SipiTestManager:
    """Controls Sipi and Nginx during tests."""

    def __init__(self, sipi_exec):
        """Reads config.ini."""

        self.config = configparser.ConfigParser()
        with open(os.path.abspath("config.ini")) as config_file:
            self.config.read_file(config_file)

        test_config = self.config["Test"]
        self.sipi_working_dir = os.path.abspath(test_config["working-dir"])

        # Ensure Sipi doesn't use caching in tests.
        sipi_cache_dir = os.path.join(self.sipi_working_dir, "cache")
        try:
            shutil.rmtree(sipi_cache_dir)
        except OSError:
            pass

        sipi_config = self.config["Sipi"]

        # use the pytest parameter '--sipi-executable-path' instead the one in config.ini
        if sipi_exec != "notset":
            self.sipi_executable = sipi_exec
        else:
            self.sipi_executable = os.path.abspath(sipi_config["sipi-executable"])

        self.sipi_config_file = sipi_config["config-file"]
        self.sipi_command = "{} --config config/{}".format(self.sipi_executable, self.sipi_config_file)
        self.data_dir = os.path.abspath(test_config["data-dir"])
        self.sipi_port = sipi_config["port"]
        self.sipi_ssl_port = sipi_config["ssl_port"]
        self.iiif_validator_prefix = sipi_config["iiif-validator-prefix"]
        self.sipi_base_url = "http://127.0.0.1:{}".format(self.sipi_port)
        self.sipi_ssl_base_url = "https://127.0.0.1:{}".format(self.sipi_ssl_port)
        self.sipi_ready_output = sipi_config["ready-output"]
        self.sipi_start_wait = int(sipi_config["start-wait"])
        self.sipi_stop_wait = int(sipi_config["stop-wait"])
        self.sipi_log_file = os.path.abspath("sipi.log")
        self.sipi_process = None
        self.sipi_started = False
        self.sipi_took_too_long = False
        self.sipi_convert_command = "{} --file {} --format {} {}"  # Braces will be replaced by actual arguments. See https://pyformat.info for details on string formatting.
        self.sipi_compare_command = "{} --compare {} {}"

        self.nginx_base_url = self.config["Nginx"]["base-url"]
        self.nginx_working_dir = os.path.abspath("nginx")
        self.start_nginx_command = "nginx -p {} -c nginx.conf".format(self.nginx_working_dir)
        self.stop_nginx_command = "nginx -p {} -c nginx.conf -s stop".format(self.nginx_working_dir)

        self.iiif_validator_command = "iiif-validate.py -s localhost:{} -p {} -i 67352ccc-d1b0-11e1-89ae-279075081939.jp2 --version=3.0 -v".format(
            self.sipi_port, self.iiif_validator_prefix)

        self.compare_command = "compare -metric {} {} {} null:"
        self.compare_out_re = re.compile(r"^(\d+) \(([0-9.]+)\).*$")
        self.info_command = "identify -verbose {}"

        self.ab_command = "ab -v 2 -c {} -n {} -s {} {}"

    def _is_port_available(self, port):
        """Check if a port is available for binding."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(1)
            result = sock.connect_ex(('127.0.0.1', port))
            return result != 0

    def _wait_for_port_available(self, port, timeout=10):
        """Wait for a port to become available."""
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self._is_port_available(port):
                return True
            time.sleep(0.5)
        return False

    def _kill_processes_on_ports(self, ports):
        """Kill processes listening on specific ports."""
        killed_any = False
        for proc in psutil.process_iter(['pid', 'name']):
            try:
                if proc.info['name'] == 'sipi':
                    # Check if this process is using any of our ports
                    try:
                        connections = proc.connections()
                        for conn in connections:
                            if hasattr(conn, 'laddr') and conn.laddr and conn.laddr.port in ports:
                                print(f"Killing process {proc.info['pid']} (sipi) using port {conn.laddr.port}", file=sys.stderr)
                                proc.terminate()
                                killed_any = True
                                break
                    except (psutil.AccessDenied, psutil.NoSuchProcess):
                        # If we can't get connections, just kill any sipi process
                        print(f"Killing sipi process {proc.info['pid']} (couldn't check connections)", file=sys.stderr)
                        proc.terminate()
                        killed_any = True
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                pass
        
        if killed_any:
            # Wait a bit for processes to terminate
            time.sleep(2)
            # Force kill any remaining processes
            for proc in psutil.process_iter(['pid', 'name']):
                try:
                    if proc.info['name'] == 'sipi':
                        print(f"Force killing remaining sipi process {proc.info['pid']}", file=sys.stderr)
                        proc.kill()
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                    pass

    def _check_sipi_health(self):
        """Check if SIPI is responding to HTTP requests."""
        try:
            # Try to connect to a IIIF info.json endpoint (more appropriate for SIPI health check)
            # This should return a 404 if the image doesn't exist, but still indicates server is responding
            response = urllib.request.urlopen(f"http://127.0.0.1:{self.sipi_port}/test/info.json", timeout=2)
            return response.getcode() in [200, 404, 400]  # Any response code indicates server is working
        except urllib.error.HTTPError as e:
            # HTTP errors (404, 400, etc.) indicate the server is responding, just not with what we want
            return e.code in [200, 404, 400, 500]  # Any HTTP response means server is working
        except (urllib.error.URLError, socket.timeout):
            # These indicate connection problems (server not responding)
            return False

    def start_sipi(self):
        """Starts Sipi and waits until it is ready to receive requests."""

        def check_for_ready_output(line):
            if self.sipi_ready_output in line:
                print(f"Found ready message: {line}", file=sys.stderr)
                self.sipi_started = True
            else:
                print(f"SIPI: {line}", file=sys.stderr)

        # Stop any existing Sipi processes and free up ports
        ports_to_check = [int(self.sipi_port), int(self.sipi_ssl_port)]
        print(f"Checking ports {ports_to_check} for existing processes...", file=sys.stderr)
        
        self._kill_processes_on_ports(ports_to_check)
        
        # Wait for ports to become available
        for port in ports_to_check:
            if not self._wait_for_port_available(port, timeout=10):
                raise SipiTestError(f"Port {port} is still in use after cleanup. Cannot start SIPI.")

        self.sipi_process = None
        self.sipi_started = False
        self.sipi_took_too_long = False

        # Remove any Sipi log file from a previous run.
        try:
            os.remove(self.sipi_log_file)
        except OSError:
            pass

        # Start a Sipi process and capture its output.
        sipi_args = shlex.split(self.sipi_command)
        sipi_start_time = time.time()
        print(f"Starting SIPI with command: {self.sipi_command}", file=sys.stderr)
        print(f"Working directory: {self.sipi_working_dir}", file=sys.stderr)
        
        try:
            # Set environment to ensure output is not buffered
            env = os.environ.copy()
            env['PYTHONUNBUFFERED'] = '1'
            
            self.sipi_process = subprocess.Popen(sipi_args,
                                                 cwd=self.sipi_working_dir,
                                                 stdout=subprocess.PIPE,
                                                 stderr=subprocess.STDOUT,
                                                 universal_newlines=True,
                                                 bufsize=0,  # Unbuffered
                                                 env=env)
        except Exception as e:
            raise SipiTestError(f"Failed to start SIPI process: {e}")
        
        # Give the process a moment to start before setting up output reader
        time.sleep(0.1)
        self.sipi_output_reader = ProcessOutputReader(self.sipi_process.stdout, check_for_ready_output)

        # Wait until Sipi says it's ready to receive requests.
        log_based_ready = False
        health_check_ready = False
        
        while (not self.sipi_took_too_long):
            # Check if process has terminated unexpectedly
            if self.sipi_process.poll() is not None:
                self.write_sipi_log()
                raise SipiTestError(
                    f"SIPI process terminated unexpectedly with exit code {self.sipi_process.returncode} (wrote {self.sipi_log_file})")
            
            # Check for log-based readiness
            if not log_based_ready and self.sipi_started:
                print("SIPI log indicates server is ready", file=sys.stderr)
                log_based_ready = True
                
            # Once we see the log message, also check health
            if log_based_ready and not health_check_ready:
                if self._check_sipi_health():
                    print("SIPI health check passed", file=sys.stderr)
                    health_check_ready = True
                    break
                    
            time.sleep(0.2)
            if time.time() - sipi_start_time > self.sipi_start_wait:
                self.sipi_took_too_long = True

        if self.sipi_took_too_long:
            # Wait a bit more for output to be collected
            time.sleep(0.5)
            self.write_sipi_log()
            
            # Get additional debug info
            process_status = "unknown"
            if self.sipi_process:
                if self.sipi_process.poll() is None:
                    process_status = "running"
                else:
                    process_status = f"exited with code {self.sipi_process.returncode}"
            
            error_msg = f"SIPI didn't start after {self.sipi_start_wait} seconds (wrote {self.sipi_log_file}). Process status: {process_status}"
            if log_based_ready:
                error_msg += ". Log indicated ready but health check failed."
            else:
                error_msg += ". No ready message found in logs."
            
            # Include some log output in the error for debugging
            log_output = self.get_sipi_output()
            if log_output:
                error_msg += f"\nLast few lines of output:\n{log_output[-500:]}"
            else:
                error_msg += "\nNo output captured."
                
            raise SipiTestError(error_msg)
        
        print(f"SIPI started successfully in {time.time() - sipi_start_time:.2f} seconds", file=sys.stderr)

    def stop_sipi(self):
        """Sends SIGTERM to Sipi and waits for it to stop."""

        if self.sipi_process is not None:
            print("Stopping SIPI process...", file=sys.stderr)
            try:
                # First try graceful shutdown
                self.sipi_process.send_signal(signal.SIGTERM)
                self.sipi_process.wait(timeout=self.sipi_stop_wait)
                print("SIPI stopped gracefully", file=sys.stderr)
            except subprocess.TimeoutExpired:
                print("SIPI didn't stop gracefully, force killing...", file=sys.stderr)
                self.sipi_process.kill()
                self.sipi_process.wait(timeout=5)
                print("SIPI force killed", file=sys.stderr)
            except Exception as e:
                print(f"Error stopping SIPI: {e}", file=sys.stderr)
            finally:
                self.sipi_process = None
                
        # Stop the output reader
        if hasattr(self, 'sipi_output_reader') and self.sipi_output_reader:
            self.sipi_output_reader.stop()
                
        # Also clean up any remaining processes on our ports
        self._kill_processes_on_ports([int(self.sipi_port), int(self.sipi_ssl_port)])
        
        self.write_sipi_log()

    def sipi_is_running(self):
        """Returns True if Sipi is running."""

        if self.sipi_process is None:
            return False
        else:
            return self.sipi_process.poll() is None

    def get_sipi_output(self):
        """Returns the output collected from Sipi"s stdout and stderr."""

        return self.sipi_output_reader.get_output()

    def start_nginx(self):
        """Starts nginx."""

        nginx_log_dir = os.path.join(self.nginx_working_dir, "logs")

        if not os.path.exists(nginx_log_dir):
            os.makedirs(nginx_log_dir)

        # Make sure nginx isn't already running.
        try:
            self.stop_nginx()
        except SipiTestError:
            pass

        nginx_args = shlex.split(self.start_nginx_command)
        if subprocess.run(nginx_args).returncode != 0:
            raise SipiTestError("nginx failed to start")

    def stop_nginx(self):
        """Stops nginx."""

        nginx_args = shlex.split(self.stop_nginx_command)
        if subprocess.run(nginx_args).returncode != 0:
            raise SipiTestError("nginx failed to stop")

    def make_sipi_url(self, url_path):
        """
        Makes a URL for a request to Sipi.

        url_path: a path that will be appended to the Sipi base URL to make the request.
        """

        return "{}{}".format(self.sipi_base_url, url_path)

    def make_sipi_ssl_url(self, url_path):
        """
        Makes a URL for a request to Sipi.

        url_path: a path that will be appended to the Sipi base URL to make the request.
        """

        return "{}{}".format(self.sipi_ssl_base_url, url_path)

    def download_file(self, url_path, suffix=None, headers=None):
        """
            Makes an HTTP request to Sipi and downloads the response content to a temporary file.
            Returns the absolute path of the temporary file.

            url_path: a path that will be appended to the Sipi base URL to make the request.
            suffix: the file extension that should be given to the temporary file.
            headers: an optional dictionary of request headers.
        """

        sipi_url = self.make_sipi_url(url_path)
        response = requests.get(sipi_url, headers=headers, stream=True)
        response.raise_for_status()
        temp_fd, temp_file_path = tempfile.mkstemp(suffix=suffix)
        temp_file = os.fdopen(temp_fd, mode="wb")

        for chunk in response.iter_content(chunk_size=8192):
            temp_file.write(chunk)

        temp_file.close()
        return temp_file_path

    def compare_server_bytes(self, url_path, expected_file_path, headers=None):
        """
            Downloads a temporary file and compares it with an existing file on disk. If the two are equivalent,
            deletes the temporary file, otherwise writes Sipi's output to sipi.log and raises an exception.

            url_path: a path that will be appended to the Sipi base URL to make the request.
            expected_file_path: the absolute path of a file containing the expected data.
            headers: an optional dictionary of request headers.
        """

        expected_file_basename, expected_file_extension = os.path.splitext(expected_file_path)
        downloaded_file_path = self.download_file(url_path, headers=headers, suffix=expected_file_extension)

        if filecmp.cmp(downloaded_file_path, expected_file_path):
            os.remove(downloaded_file_path)
        else:
            raise SipiTestError(
                "Downloaded file {} is different from expected file {} (wrote {})".format(downloaded_file_path,
                                                                                          expected_file_path,
                                                                                          self.sipi_log_file))

    def compare_server_images(self, url_path, expected_file_path, headers=None):
        """
            Downloads a temporary image and compares it with an existing image on disk. If the two are equivalent,
            deletes the temporary file, otherwise writes Sipi's output to sipi.log and raises an exception.

            url_path: a path that will be appended to the Sipi base URL to make the request.
            expected_file_path: the absolute path of a file containing the expected data.
            headers: an optional dictionary of request headers.
        """

        expected_file_basename, expected_file_extension = os.path.splitext(expected_file_path)
        downloaded_file_path = self.download_file(url_path, headers=headers, suffix=expected_file_extension)
        compare_process_args = shlex.split(
            self.sipi_compare_command.format(self.sipi_executable, downloaded_file_path, expected_file_path))
        compare_process = subprocess.run(compare_process_args,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT,
                                         universal_newlines=True)

        if compare_process.returncode != 0:
            raise SipiTestError(
                "Sipi compare: pixels not identical {} {}:\n {}".format(downloaded_file_path, expected_file_path, compare_process.stdout))

    def expect_status_code(self, url_path, status_code, headers=None, method='get'):
        """
        Requests a file and expects to get a particular HTTP status code.

        url_path: a path that will be appended to the Sipi base URL to make the request.
        status_code: the expected status code.
        headers: an optional dictionary of request headers.
        """

        sipi_url = self.make_sipi_url(url_path)
        response = getattr(requests, method)(sipi_url, headers=headers)

        if response.status_code != status_code:
            format = "Received status code {} for URL {}, expected {} (wrote {}). Response:\n{}"
            raise SipiTestError(format.format(response.status_code, sipi_url, status_code, self.sipi_log_file, response.text))

        return response

    def get_image_info(self, url_path, headers=None):
        """
            Downloads a temporary image file, gets information about it using ImageMagick's 'identify'
            program with the '-verbose' option, and returns the resulting output.

            url_path: a path that will be appended to the Sipi base URL to make the request.
            headers: an optional dictionary of request headers.
        """

        downloaded_file_path = self.download_file(url_path, headers=headers)
        info_process_args = shlex.split(self.info_command.format(downloaded_file_path))
        info_process = subprocess.run(info_process_args,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT,
                                      universal_newlines=True)
        return info_process.stdout

    def sipi_convert(self, source_file_path, target_file_path, target_file_format):
        """
            Runs Sipi on the command line to convert an image from one format to another.

            source_file_path: the absolute path of the source file.
            target_file_path: the absolute path of the target file.
            target_file_format: jpx, jpg, tif, or png.
        """
        convert_process_args = shlex.split(
            self.sipi_convert_command.format(self.sipi_executable, source_file_path, target_file_format,
                                             target_file_path))
        convert_process = subprocess.run(convert_process_args,
                                         cwd=self.sipi_working_dir,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT,
                                         universal_newlines=True)

        if convert_process.returncode != 0:
            raise SipiTestError(
                "Error converting {} to {}:\n{}".format(source_file_path, target_file_path, convert_process.stdout))

    def compare_images(self, reference_target_file_path, converted_file_path, metric):
        """
            Checks the distortion in converted image by comparing it with a reference image, using ImageMagick's
            'compare' program. Returns an integer representing the result.

            reference_target_file_path: the absolute path of the reference image file.
            converted_file_path: the absolute path of the image to be checked.
            metric: the type of comparison to be performed, either 'MAE' or 'PAE'.
        """

        compare_process_args = shlex.split(
            self.compare_command.format(metric, reference_target_file_path, converted_file_path))
        compare_process = subprocess.run(compare_process_args,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT,
                                         universal_newlines=True)

        compare_out_str = compare_process.stdout
        compare_out_regex_match = self.compare_out_re.match(compare_out_str)
        assert compare_out_regex_match != None, "Couldn't parse comparison result: {}".format(compare_out_str)
        return int(compare_out_regex_match.group(1))

    def data_dir_path(self, relative_path):
        """
            Converts a path relative to data-dir into an absolute path.
        """
        return os.path.join(self.data_dir, relative_path)

    def post_file(self, url_path, file_path, mime_type, params=None, headers=None):
        """
            Uploads a file to Sipi using HTTP POST with with Content-Type: multipart/form-data. Returns the parsed JSON of Sipi's response.

            url_path: a path that will be appended to the Sipi base URL to make the request.
            file_path: the absolute path to the file to be uploaded.
            params: the parameters to be sent with the request (dict).
            headers: an optional dictionary of request headers.
            :return: the json response as a dict.
        """

        sipi_url = self.make_sipi_url(url_path)

        with open(file_path, "rb") as file_obj:
            files = {"file": (os.path.basename(file_path), file_obj, mime_type)}
            try:
                response = requests.post(sipi_url, files=files, data=params, headers=headers)
                response.raise_for_status()
            except Exception as err:
                print("**************************************************************************\n")
                print('ERROR:', err)
                print("**************************************************************************\n")
                #raise SipiTestError("post request with image file to {} failed: {}".format(sipi_url, response.json()["message"]))
                raise SipiTestError("post request with image file to {} failed: ".format(sipi_url))

        return response.json()

    def get_json(self, url_path, use_ssl=False, use_forwarded_ssl=None):
        """
        Sends a request which expects JSON
        :param url_path: a path that will be appended to the Sipi base URL to make the request.
        :return: the json response as a dict.
        """

        if use_ssl:
            sipi_url = self.make_sipi_ssl_url(url_path)
        else:
            sipi_url = self.make_sipi_url(url_path)

        x_forwarded_proto = {True: 'https', False: 'http'}.get(use_forwarded_ssl)

        try:
            response = requests.get(sipi_url, headers={'X-Forwarded-Proto': x_forwarded_proto})
            response.raise_for_status()
        except:
            raise SipiTestError("post request to {} failed: {}".format(sipi_url, response.json()["message"]))
        return response.json()

    def get_auth_json(self, url_path, use_ssl=False):
        if use_ssl:
            sipi_url = self.make_sipi_ssl_url(url_path)
        else:
            sipi_url = self.make_sipi_url(url_path)

        try:
            response = requests.get(sipi_url)
            if response.status_code != 401:
                raise SipiTestError("Get of IIIF Auth info.json {} failed: no 401 error!".format(sipi_url))
        except:
            raise SipiTestError("Get of IIIF Auth info.json {} failed: {}".format(sipi_url, response.json()["message"]))
        return response.json()

    def post_request(self, url_path, params, headers=None):
        """
        Sends a post request to a Sipi route (without binaries).

        :param url_path: a path that will be appended to the Sipi base URL to make the request.
        :param params: the parameters to be sent with the request (dict).
        :param headers: an optional dictionary of request headers.
        :return: the json response as a dict.
        """

        sipi_url = self.make_sipi_url(url_path)

        try:
            response = requests.post(sipi_url, data=params, headers=headers)
            response.raise_for_status()
        except Exception as err:
            print("**************************************************************************\n")
            print(err)
            print("**************************************************************************\n")
            raise SipiTestError("post request to {} failed: {}".format(sipi_url, response.json()["message"]))
        return response.json()

    def write_sipi_log(self):
        """Writes Sipi's output to a log file."""

        with open(self.sipi_log_file, "w") as file:
            file.write(self.get_sipi_output())

    def run_iiif_validator(self):
        """Runs the IIIF validator. If validation fails, writes Sipi's output to sipi.log and raises an exception."""

        validator_process_args = shlex.split(self.iiif_validator_command)
        validator_process = subprocess.run(validator_process_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True)

        if validator_process.returncode != 0:
            raise SipiTestError(
                "IIIF validation failed (wrote {}):\n{}".format(self.sipi_log_file, validator_process.stdout))

    def download_file_to_data_dir_tmp(self, url, file_extension=".bin"):
        """Only downloads a file to the target directory if not already there"""
        url_hash = hashlib.md5(url.encode('utf-8')).hexdigest()
        test_tmp_directory = self.data_dir_path("test_tmp")

        if not os.path.exists(test_tmp_directory):
            os.makedirs(test_tmp_directory)

        local_file_path = test_tmp_directory + "/" + url_hash + file_extension

        if not os.path.isfile(local_file_path):
            r = requests.get(url, stream=True)
            with open(local_file_path, 'wb') as f:
                for chunk in r.iter_content(chunk_size=1024):
                    if chunk:
                        f.write(chunk)
            return local_file_path
        else:
            return local_file_path

    def run_ab(self, concurrent_requests, total_requests, timeout, url_path):
        """
        Runs Apache ab to test the Sipi server.

        concurrent_requests: the number of concurrent requests to run.
        total_requests: the total number of requests to run.
        timeout: the request timeout in seconds.
        url_path: the Sipi URL path, to be appended to the Sipi base URL.
        """

        sipi_url = self.make_sipi_url(url_path)
        formatted_ab_command = self.ab_command.format(concurrent_requests, total_requests, timeout, sipi_url)
        ab_args = shlex.split(formatted_ab_command)

        return subprocess.Popen(ab_args,
                                cwd=self.sipi_working_dir,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)  # redirect stderr to stdout

    def cleanup(self):
        """Cleanup all files created"""
        fileList = glob.glob(self.data_dir + '/knora/_*')
        for filePath in fileList:
            os.remove(filePath)

        fileList = glob.glob(self.data_dir + '/unit/_*')
        for filePath in fileList:
            os.remove(filePath)

        fileList = glob.glob(self.data_dir + '/thumbs/_*')
        for filePath in fileList:
            os.remove(filePath)


class SipiTestError(Exception):
    """Indicates an error in a Sipi test."""


class ProcessOutputReader:
    """Spawns a thread that collects the output of a subprocess."""

    def __init__(self, stream, line_func):
        self.stream = stream
        self.line_func = line_func
        self.lines = []
        self.lock = threading.Lock()
        self.running = True

        def collect_lines():
            try:
                while self.running:
                    line = self.stream.readline()
                    if line:
                        with self.lock:
                            self.lines.append(line)
                        # Call line_func outside the lock to avoid deadlock
                        if self.line_func:
                            self.line_func(line.rstrip())
                    else:
                        # Stream closed or EOF
                        break
            except Exception as e:
                print(f"ProcessOutputReader error: {e}", file=sys.stderr)
            finally:
                with self.lock:
                    self.running = False

        self.thread = threading.Thread(target=collect_lines)
        self.thread.daemon = True
        self.thread.start()

    def get_output(self):
        with self.lock:
            return "".join(self.lines)
    
    def stop(self):
        """Stop the output reader and wait for thread to finish."""
        self.running = False
        if self.thread.is_alive():
            self.thread.join(timeout=2)


def pytest_itemcollected(item):
    """Outputs test class and function docstrings, if provided, when each test is run."""

    par = item.parent.obj
    node = item.obj
    pref = par.__doc__.strip() if par.__doc__ else par.__class__.__name__
    component = par.component
    suf = node.__doc__.strip() if node.__doc__ else node.__name__
    if pref or suf:
        item._nodeid = "{}: {} should {}".format(pref, component, suf)
