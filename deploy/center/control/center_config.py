"""Read center credentials and build the vmauth routes shared by both deployment modes."""

import json
import re
from urllib.parse import urlsplit

TOKEN = re.compile(r"[A-Za-z0-9._~+/=-]{32,256}")
SCOPE = re.compile(r"[A-Za-z0-9_.-]{1,128}")
ACCOUNT = re.compile(r"0|[1-9][0-9]{0,9}")
READ_ENDPOINTS = ("query", "query_range", "series", "labels", "status/buildinfo", "label/[a-zA-Z_][a-zA-Z0-9_]*/values")


def check_tokens(tokens):
    for token in tokens:
        if not isinstance(token, str) or token.startswith("REPLACE_") or not TOKEN.fullmatch(token):
            raise ValueError("Replace each placeholder with a 32-256 character Bearer token")
    if len(set(tokens)) != len(tokens):
        raise ValueError("Use distinct tokens for each tenant, permission and I8 scope")


def check_keys(values, allowed, prefix=""):
    unknown = sorted(values.keys() - set(allowed))
    if unknown:
        raise ValueError(f"Unknown configuration field: {prefix}{unknown[0]}")


def load_config(path):
    config = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("Center configuration must be a JSON object")
    check_keys(config, ("tenants", "i8_tokens", "retention_period", "public_write_origin", "scope_accounts", "cluster"))
    tenants, scopes = config.get("tenants"), config.get("i8_tokens")
    if not isinstance(tenants, dict) or not tenants or not isinstance(scopes, dict) or not scopes:
        raise ValueError("Configure at least one tenant and one I8 scope")

    tokens = []
    for account, credentials in tenants.items():
        if not ACCOUNT.fullmatch(account) or int(account) > 2**32 - 1:
            raise ValueError("Tenant keys must be canonical VictoriaMetrics account IDs")
        if isinstance(credentials, dict):
            check_keys(credentials, ("read_token", "write_token"), f"tenants.{account}.")
        if not isinstance(credentials, dict) or credentials.keys() != {"write_token", "read_token"}:
            raise ValueError("Each tenant needs a write_token and read_token")
        tokens.extend(credentials.values())
    for scope, values in scopes.items():
        if not SCOPE.fullmatch(scope):
            raise ValueError("Invalid I8 scope")
        if isinstance(values, str):
            values = [values]
        if not isinstance(values, list) or not 1 <= len(values) <= 2:
            raise ValueError("Each I8 scope needs one token or two tokens during rotation")
        tokens.extend(values)
    check_tokens(tokens)
    registration_urls(config)
    return config


def registration_urls(config):
    mapping = config.get("scope_accounts", {})
    if not isinstance(mapping, dict):
        raise ValueError("scope_accounts must map control scopes to tenant IDs")
    if not mapping:
        return {}
    origin = config.get("public_write_origin", "")
    url = urlsplit(origin)
    has_credentials = url.username is not None or url.password is not None
    has_resource = url.path not in ("", "/") or url.query or url.fragment
    if url.scheme != "https" or not url.hostname or has_credentials or has_resource or url.port == 0:
        raise ValueError("public_write_origin must be an external HTTPS origin")
    urls = {}
    for scope, account in mapping.items():
        if scope not in config["i8_tokens"] or not isinstance(account, str) or account not in config["tenants"]:
            raise ValueError("Unknown scope or account in scope_accounts")
        urls[scope] = f"{origin.rstrip('/')}/insert/{account}/prometheus/api/v1/write"
    return urls


def retention_period(config):
    retention = config.get("retention_period", "7d")
    if not isinstance(retention, str) or not re.fullmatch(r"[1-9][0-9]{0,4}[dwy]", retention):
        raise ValueError("retention_period must be a positive duration such as 7d")
    return retention


def auth_config(tenants, write_origin, read_origin):
    users = []
    for account, credentials in sorted(tenants.items()):
        routes = (
            ("write", f"/insert/{account}/prometheus/api/v1", ("write",), write_origin),
            ("read", f"/select/{account}/prometheus/api/v1", READ_ENDPOINTS, read_origin),
        )
        for role, prefix, endpoints, origin in routes:
            users.append(
                {
                    "name": f"tenant-{account}-{role}",
                    "bearer_token": credentials[f"{role}_token"],
                    "url_map": [{"src_paths": [f"{prefix}/{name}" for name in endpoints], "url_prefix": origin}],
                }
            )
    return json.dumps({"users": users})
