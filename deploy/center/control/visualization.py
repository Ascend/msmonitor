"""Start the bundled dashboards for one configured center tenant."""

import os
import sys
from pathlib import Path

from center_config import READ_ENDPOINTS

ASSETS = Path("/opt/msmonitor-visualization")
GRAFANA_HOME = Path("/usr/share/grafana")
GRAFANA_DATA = Path("/data/grafana")
QUERY_ADDRESS = "127.0.0.1:19104"
SUMMARY_PORT = "19103"
HEALTH_URLS = {
    "summary": f"http://127.0.0.1:{SUMMARY_PORT}/health",
    "grafana": "http://127.0.0.1:3000/api/health",
}


def query_proxy(config, auth_address):
    tenants = config["tenants"]
    account = os.environ.get("MSMONITOR_GRAFANA_TENANT")
    if not account:
        if len(tenants) != 1:
            raise ValueError("Set MSMONITOR_GRAFANA_TENANT when center.json defines multiple tenants")
        account = next(iter(tenants))
    if account not in tenants:
        raise ValueError("MSMONITOR_GRAFANA_TENANT must identify a tenant in center.json")
    token = tenants[account]["read_token"]
    paths = " ".join(f"^/api/v1/{endpoint}$" for endpoint in READ_ENDPOINTS)
    return f"""
frontend grafana_query
  bind {QUERY_ADDRESS}
  http-request deny deny_status 404 unless {{ path_reg {paths} }}
  http-request set-header Authorization "Bearer {token}"
  http-request set-path /select/{account}/prometheus%[path]
  default_backend grafana_query_auth

backend grafana_query_auth
  server vmauth {auth_address}
"""


def start(processes):
    password_file = Path("/etc/msmonitor/grafana-admin-password")
    password = password_file.read_text(encoding="utf-8").strip()
    if len(password) < 12:
        raise ValueError("Grafana administrator password must contain at least 12 characters")
    GRAFANA_DATA.mkdir(exist_ok=True)
    query_env = {
        **os.environ,
        "MSMONITOR_PROMETHEUS_URL": f"http://{QUERY_ADDRESS}",
        "MSMONITOR_SUMMARY_PORT": SUMMARY_PORT,
    }
    processes.start(
        [sys.executable, str(ASSETS / "backend/server.py")], query_env, readiness_url=HEALTH_URLS["summary"]
    )
    grafana_env = {
        **query_env,
        "GF_PATHS_DATA": str(GRAFANA_DATA),
        "GF_PATHS_PROVISIONING": str(ASSETS / "provisioning"),
        "GF_PATHS_PLUGINS": str(ASSETS / "plugins"),
        "GF_PATHS_LOGS": str(GRAFANA_DATA / "log"),
        "GF_SERVER_HTTP_PORT": "3000",
        "GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH": "/etc/grafana/dashboards/l1.json",
        "GF_PLUGINS_ALLOW_LOADING_UNSIGNED_PLUGINS": "msmonitor-step-panel,msmonitor-summary-datasource",
        "GF_AUTH_ANONYMOUS_ENABLED": "false",
        "GF_USERS_ALLOW_SIGN_UP": "false",
        "GF_SECURITY_ADMIN_PASSWORD": password,
    }
    processes.start(
        [str(GRAFANA_HOME / "bin/grafana"), "server", "--homepath", str(GRAFANA_HOME)],
        grafana_env,
        readiness_url=HEALTH_URLS["grafana"],
    )
