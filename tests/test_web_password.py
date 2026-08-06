#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
api = (root / "mk-piclock-api.c").read_text(encoding="utf-8")
app = (root / "web/assets/js/app.js").read_text(encoding="utf-8")
index = (root / "web/index.html").read_text(encoding="utf-8")
system_html = (root / "web/modules/system/module.html").read_text(encoding="utf-8")
system_js = (root / "web/modules/system/module.js").read_text(encoding="utf-8")
makefile = (root / "Makefile").read_text(encoding="utf-8")
install = (root / "install.md").read_text(encoding="utf-8")
addon = (root / "ADDON_API.md").read_text(encoding="utf-8")
openapi = json.loads((root / "api/openapi-v1.json").read_text(encoding="utf-8"))

for route in (
    "/api/v1/auth/status",
    "/api/v1/auth/login",
    "/api/v1/auth/password",
):
    assert route in api, f"missing API route {route}"
    assert route in openapi["paths"], f"missing OpenAPI route {route}"

assert '#define WEB_PASSWORD_FILE "/opt/mk-piclock/config/web-password.txt"' in api
assert '#define WEB_PASSWORD_MAX 64' in api
assert 'HttpOnly; SameSite=Strict' in api
assert r'\"storage\":\"plaintext\"' in api
assert 'int public_api =' in api
assert 'MHD_HTTP_UNAUTHORIZED' in api
assert 'Password required' in api
assert 'Password is incorrect' in api
assert 'fsync(fd)' in api
assert 'O_NOFOLLOW' in api

assert 'id="auth-gate"' in index
assert 'id="auth-password"' in index
assert 'ensureAuthenticated()' in app
assert "credentials: 'same-origin'" in app
assert "response.status === 401" in app
assert "'/api/v1/auth/login'" in app
assert 'id="password-form"' in system_html
assert 'id="remove-password"' in system_html
assert 'stored as plain text' in system_html
assert "ctx.json('/api/v1/auth/status'" in system_js
assert "ctx.update('/api/v1/auth/password'" in system_js
assert 'sudo chmod -R u=rwX,g=rwX,o= /opt/mk-piclock/config' in makefile
assert '/opt/mk-piclock/config/web-password.txt' in install
assert 'Reset a lost web password' in install
assert 'mkpiclock_auth' in addon

print("web password checks passed")
