import os
import requests
import time
import subprocess

from pathlib import Path
from testcontainers.core.container import DockerContainer


def setup_module():
    """ Setup for the Sipi container used in tests. """
    project_source_dir = Path(__file__).parent.parent.parent
    config_dir = project_source_dir / "config"
    images_dir = project_source_dir / "test/_test_data/images"
    scripts_dir = project_source_dir / "scripts"
    server_dir = project_source_dir / "server"

    container = DockerContainer("daschswiss/sipi:latest")
    container.with_bind_ports(1024, 1024)
    container.with_volume_mapping(str(config_dir), "/sipi/config")
    container.with_volume_mapping(str(images_dir), "/sipi/images")
    container.with_volume_mapping(str(scripts_dir), "/sipi/scripts")
    container.with_volume_mapping(str(server_dir), "/sipi/server")

    return container


def get_logs(container_id):
    logs_cmd = "docker logs " + container_id
    result = subprocess.run(
        logs_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout, result.stderr


def test_get_image():
    """ This test gets the lena512.jp2 image. """
    container = setup_module()
    with container:
        time.sleep(1)
        r = requests.get("http://localhost:1024/unit/lena512.jp2/full/max/0/default.jpg")
        assert r.status_code == 200


def test_get_test_html():
    """ This test gets the test.html file. """
    container = setup_module()
    with container:
        time.sleep(1)
        r = requests.get("http://localhost:1024/server/test.html")
        assert r.status_code == 200


def test_luafunctions():
    """ This tests the call to /test/lua_functions """
    container = setup_module()
    with container:
        time.sleep(1)
        r = requests.get("http://localhost:1024/test/luafunctions")
        assert r.status_code == 200
