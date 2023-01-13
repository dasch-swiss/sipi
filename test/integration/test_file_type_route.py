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
    container.with_volume_mapping(config_dir, "/sipi/config")
    container.with_volume_mapping(images_dir, "/sipi/images")
    container.with_volume_mapping(scripts_dir, "/sipi/scripts")
    container.with_volume_mapping(server_dir, "/sipi/server")

    return container


def get_logs(container_id):
    logs_cmd = "docker logs " + container_id
    result = subprocess.run(
        logs_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout, result.stderr


def test_get_test_html():
    """ This test gets the test.html file. """
    container = setup_module()
    with container:
        time.sleep(1)
        cid = container._container.short_id
        r = requests.get("http://localhost:1024/server/test.html")
        if r.status_code != 200:
            get_logs(cid)
        assert r.status_code == 200


def test_file_type():
    """ This tests the call to /test_file_type """
    container = setup_module()
    with container:
        time.sleep(1)
        cid = container._container.short_id
        r = requests.get("http://localhost:1024/test_file_type")
        if r.status_code != 200:
            get_logs(cid)
        assert r.status_code == 200


def test_functions():
    """ This tests the call to /test_functions """
    container = setup_module()
    with container:
        time.sleep(1)
        cid = container._container.short_id
        r = requests.get("http://localhost:1024/test_functions")
        if r.status_code != 200:
            get_logs(cid)
        assert r.status_code == 200
