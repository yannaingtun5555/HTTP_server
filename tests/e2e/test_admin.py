import pytest
import requests

ADMIN_URL = "http://localhost:8080"
PROXY_ADMIN_URL = "http://localhost:8000/_admin"

def test_django_health_check():
    """Verify Django landing/healthcheck page returns 200 OK."""
    try:
        r = requests.get(f"{ADMIN_URL}/", timeout=5)
        assert r.status_code == 200
        assert "Django Container Running" in r.text
    except requests.ConnectionError:
        pytest.skip("Django admin panel container on port 8080 is not running.")

def test_django_dashboard_endpoint():
    """Verify Django dashboard endpoint returns 200 OK."""
    try:
        r = requests.get(f"{ADMIN_URL}/_admin/", timeout=5)
        assert r.status_code == 200
        assert "Sites Dashboard" in r.text
    except requests.ConnectionError:
        pytest.skip("Django admin panel container on port 8080 is not running.")

def test_django_add_site_step1():
    """Verify Add Site Step 1 form page loads correctly."""
    try:
        r = requests.get(f"{ADMIN_URL}/_admin/add/", timeout=5)
        assert r.status_code == 200
        assert "Add New Site" in r.text
        assert "csrfmiddlewaretoken" in r.text
    except requests.ConnectionError:
        pytest.skip("Django admin panel container on port 8080 is not running.")

def test_github_webhook():
    """Verify GitHub/GitLab webhook receiver endpoint."""
    try:
        r = requests.post(f"{ADMIN_URL}/_admin/webhook/example.com/", headers={"X-GitHub-Event": "push"}, timeout=5)
        assert r.status_code == 200
        data = r.json()
        assert data["success"] is True
        assert "Auto-deploy triggered for example.com" in data["message"]
    except requests.ConnectionError:
        pytest.skip("Django admin panel container on port 8080 is not running.")
