import pytest
import requests
import subprocess
import time
import os
import shutil

SERVER_PORT = 8080
INTERNAL_SECRET = "supersecret_internal_key"
API_URL = f"http://localhost:{SERVER_PORT}/internal/api"

@pytest.fixture(scope="session", autouse=True)
def setup_server():
    # Setup test directories
    os.makedirs("test_sites_root", exist_ok=True)
    os.makedirs("test_logs", exist_ok=True)

    # Generate dummy configs
    with open("server_test.conf", "w") as f:
        f.write("""
server_port=8080
sites_root=test_sites_root
internal_api_secret=supersecret_internal_key
internal_api_path_prefix=/internal/api
server_db_host=localhost
server_db_port=5432
server_db_name=http_server
server_db_user=postgres
server_db_password=postgres
""")
    with open("sites_test.conf", "w") as f:
        f.write("{}")

    # Start server
    binary_path = "./build/HTTP_Server"
    
    if not os.path.exists(binary_path):
        pytest.skip(f"{binary_path} not found. Please compile the project first.")

    # Note: HTTP_Server might require the config file path as an argument or reads from PWD
    # We will assume it reads server.conf if no args provided, so we rename server_test to server.conf
    # For safety, let's just copy it to server.conf (assuming we backup original if needed)
    if os.path.exists("server.conf"):
        os.rename("server.conf", "server.conf.bak")
    shutil.copy("server_test.conf", "server.conf")
    
    if os.path.exists("sites.conf"):
        os.rename("sites.conf", "sites.conf.bak")
    shutil.copy("sites_test.conf", "sites.conf")

    try:
        process = subprocess.Popen(
            [binary_path],
            stdout=open("test_logs/server.out", "w"),
            stderr=open("test_logs/server.err", "w")
        )
        
        # Wait for server to be ready
        for _ in range(15):
            try:
                r = requests.get(f"{API_URL}/status", headers={"X-Internal-Secret": INTERNAL_SECRET})
                if r.status_code == 200:
                    break
            except requests.ConnectionError:
                pass
            time.sleep(1)
        else:
            process.kill()
            raise RuntimeError("Server did not start in time. Check test_logs/server.err")

        yield
    finally:
        # Teardown
        if 'process' in locals() and process:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
        
        # Clean up
        if os.path.exists("server_test.conf"): os.remove("server_test.conf")
        if os.path.exists("sites_test.conf"): os.remove("sites_test.conf")
        
        if os.path.exists("server.conf.bak"):
            if os.path.exists("server.conf"): os.remove("server.conf")
            os.rename("server.conf.bak", "server.conf")
        if os.path.exists("sites.conf.bak"):
            if os.path.exists("sites.conf"): os.remove("sites.conf")
            os.rename("sites.conf.bak", "sites.conf")
            
        shutil.rmtree("test_sites_root", ignore_errors=True)

def test_status_endpoint():
    r = requests.get(f"{API_URL}/status", headers={"X-Internal-Secret": INTERNAL_SECRET})
    assert r.status_code == 200
    data = r.json()
    assert data["success"] is True
    assert "server" in data

def test_unauthorized():
    r = requests.get(f"{API_URL}/status")
    assert r.status_code == 401
    assert r.json()["success"] is False

def test_deploy_missing_db_creds():
    payload = {
        "domain": "test.com",
        "fe_folder": "/tmp/fe",
        "be_folder": "/tmp/be",
        "be_type": "node",
        "dbs": [{"db_alias": "main", "db_type": "postgres"}]
    }
    r = requests.post(f"{API_URL}/deploy", json=payload, headers={"X-Internal-Secret": INTERNAL_SECRET})
    # Since Postgres/K8s are likely not configured or mocked, this should fail gracefully rather than crashing
    assert r.status_code == 500
    assert r.json()["success"] is False
    assert "error" in r.json()

def test_command_injection_safeguard():
    # Attempt command injection on fe_folder
    payload = {
        "domain": "inject.com",
        "fe_folder": ".; rm -rf /tmp/inject_test",
        "be_folder": "/tmp/be",
        "be_type": "static",
        "fe_build": "none",
        "dbs": []
    }
    # It should not execute the rm command. It should fail finding the directory.
    r = requests.post(f"{API_URL}/deploy", json=payload, headers={"X-Internal-Secret": INTERNAL_SECRET})
    assert r.status_code == 500
    # Because fe_folder doesn't exist, it should return an error.
    assert "FE source folder does not exist" in r.json().get("error", "")
