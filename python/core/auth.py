"""SAML authentication via Playwright with heuristic form handling."""

from __future__ import annotations

import base64
import json
import os
import re
import ssl
import threading
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from typing import Optional

from playwright.sync_api import sync_playwright

from .totp import generate_totp


def _detect_desktop_user() -> Optional[str]:
    """Detect the active desktop user when running as root."""
    import glob
    import subprocess

    for user_dir in glob.glob("/run/user/*"):
        try:
            uid = int(os.path.basename(user_dir))
            if uid >= 1000:
                import pwd
                user = pwd.getpwuid(uid).pw_name
                session_dir = f"/home/{user}/.cache/gnome-vpn-sso/browser-session"
                if os.path.isdir(session_dir):
                    return user
        except Exception:
            continue

    try:
        result = subprocess.run(
            ["loginctl", "list-sessions", "--no-legend"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            for line in result.stdout.strip().split("\n"):
                parts = line.split()
                if len(parts) >= 3:
                    session_id = parts[0]
                    user = parts[2]
                    type_result = subprocess.run(
                        ["loginctl", "show-session", session_id, "-p", "Type"],
                        capture_output=True,
                        text=True,
                        timeout=5,
                    )
                    if "x11" in type_result.stdout or "wayland" in type_result.stdout:
                        return user
    except Exception:
        pass

    try:
        result = subprocess.run(["who"], capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            for line in result.stdout.strip().split("\n"):
                if "(:0)" in line or "(:" in line:
                    return line.split()[0]
    except Exception:
        pass

    return None


def _get_gp_prelogin(server: str, debug: bool = False) -> tuple[Optional[str], Optional[str], Optional[str]]:
    """Get prelogin-cookie and SAML request for GlobalProtect."""
    url = f"https://{server}/global-protect/prelogin.esp?tmp=tmp&clientVer=4100&clientos=Linux"
    try:
        ctx = ssl.create_default_context()
        req = urllib.request.Request(url)
        req.add_header("User-Agent", "PAN GlobalProtect")
        with urllib.request.urlopen(req, timeout=10, context=ctx) as resp:
            if resp.status != 200:
                return None, None, None
            content = resp.read().decode("utf-8")
            root = ET.fromstring(content)
            prelogin_cookie = None
            saml_request = None
            gateway_ip = None
            for elem in root.iter():
                if elem.tag == "prelogin-cookie":
                    prelogin_cookie = elem.text
                elif elem.tag == "saml-request":
                    saml_request = elem.text
                elif elem.tag == "server-ip":
                    gateway_ip = elem.text
            return prelogin_cookie, saml_request, gateway_ip
    except Exception as exc:
        if debug:
            print(f"[DEBUG] prelogin.esp error: {exc}")
        return None, None, None


def do_saml_auth(
    vpn_server: str,
    username: str,
    password: str,
    totp_secret: Optional[str] = None,
    protocol: str = "anyconnect",
    auto_totp: bool = True,
    headless: bool = True,
    debug: bool = False,
    vpn_server_ip: Optional[str] = None,
):
    """Complete Microsoft SAML authentication and return cookies."""
    vpn_server_raw = vpn_server
    try:
        parsed_server = urllib.parse.urlparse(vpn_server_raw if "://" in vpn_server_raw else f"//{vpn_server_raw}")
        vpn_server_host = parsed_server.hostname or vpn_server_raw
        vpn_server_netloc = parsed_server.netloc or vpn_server_raw
    except Exception:
        vpn_server_host = vpn_server_raw
        vpn_server_netloc = vpn_server_raw

    vpn_url = f"https://{vpn_server_netloc}"

    gp_prelogin_cookie, gp_saml_request, gp_gateway_ip = None, None, None
    if protocol == "gp":
        print("  [1/6] Getting GlobalProtect prelogin info...")
        gp_prelogin_cookie, gp_saml_request, gp_gateway_ip = _get_gp_prelogin(vpn_server, debug)
        if debug:
            print(f"    [DEBUG] prelogin-cookie: {gp_prelogin_cookie[:20] if gp_prelogin_cookie else None}...")
            print(f"    [DEBUG] gateway_ip: {gp_gateway_ip}")
    else:
        print("  [1/6] Using AnyConnect SAML URL...")

    real_user = os.environ.get("SUDO_USER", os.environ.get("USER", "root"))
    home = os.path.expanduser("~")
    if real_user == "root":
        detected_user = _detect_desktop_user()
        if detected_user:
            real_user = detected_user
            if debug:
                print(f"    [DEBUG] Detected desktop user: {real_user}")
    if real_user != "root":
        try:
            import pwd
            home = pwd.getpwnam(real_user).pw_dir
            os.environ.setdefault("PLAYWRIGHT_BROWSERS_PATH", f"{home}/.cache/ms-playwright")
        except Exception:
            pass
    else:
        for pw_path in ["/var/cache/ms-playwright", "/opt/ms-playwright", "/usr/share/ms-playwright"]:
            if os.path.isdir(pw_path):
                os.environ.setdefault("PLAYWRIGHT_BROWSERS_PATH", pw_path)
                break

    with sync_playwright() as p:
        if real_user != "root":
            cache_dir = os.path.join(home, ".cache", "gnome-vpn-sso", "browser-session")
        else:
            cache_dir = None
            for base in ["/var/cache", "/tmp"]:
                test_dir = os.path.join(base, "gnome-vpn-sso", "browser-session")
                try:
                    os.makedirs(test_dir, exist_ok=True)
                    test_file = os.path.join(test_dir, ".write-test")
                    with open(test_file, "w") as f:
                        f.write("test")
                    os.remove(test_file)
                    cache_dir = test_dir
                    break
                except Exception:
                    continue
            if not cache_dir:
                cache_dir = f"/tmp/gnome-vpn-sso-{os.getpid()}/browser-session"
        os.makedirs(cache_dir, exist_ok=True)

        context = p.chromium.launch_persistent_context(
            cache_dir,
            headless=headless,
            args=["--no-sandbox", "--disable-dev-shm-usage"],
            user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        )
        page = context.pages[0] if context.pages else context.new_page()

        saml_result = {
            "prelogin_cookie": None,
            "saml_username": None,
            "saml_response": None,
            "portal_userauthcookie": None,
        }

        allowed_hosts = {vpn_server_host}
        if gp_gateway_ip:
            allowed_hosts.add(gp_gateway_ip)
        if vpn_server_ip:
            allowed_hosts.add(vpn_server_ip)

        def _is_vpn_url(url: str) -> bool:
            try:
                host = urllib.parse.urlparse(url).hostname or ""
            except Exception:
                host = ""
            return host in allowed_hosts

        def _cookie_domain_matches(domain: str) -> bool:
            domain_no_dot = domain.lstrip(".")
            if domain_no_dot == vpn_server_host:
                return True
            if vpn_server_host.endswith(f".{domain_no_dot}"):
                return True
            if vpn_server_ip and domain_no_dot == vpn_server_ip:
                return True
            return False

        vpn_request_event = threading.Event()

        def _wait_for_vpn_callback(timeout_ms: int = 60000) -> None:
            if _is_vpn_url(page.url):
                return
            if saml_result.get("saml_response") or saml_result.get("prelogin_cookie") or saml_result.get("portal_userauthcookie"):
                return
            deadline = time.time() + (timeout_ms / 1000.0)
            while time.time() < deadline:
                if saml_result.get("saml_response") or saml_result.get("prelogin_cookie") or saml_result.get("portal_userauthcookie"):
                    return
                if _is_vpn_url(page.url):
                    return
                if vpn_request_event.wait(timeout=0.25):
                    return

        def handle_request(request):
            if _is_vpn_url(request.url):
                vpn_request_event.set()
                if debug:
                    print(f"    [DEBUG] Request to VPN: {request.url[:80]}...")
                    print(f"    [DEBUG] Request method: {request.method}")
                if request.post_data:
                    try:
                        params = urllib.parse.parse_qs(request.post_data)
                        if debug:
                            print(f"    [DEBUG] POST params: {list(params.keys())}")
                        if "SAMLResponse" in params:
                            saml_result["saml_response"] = params["SAMLResponse"][0]
                            if debug:
                                print(f"    [DEBUG] Captured SAMLResponse ({len(saml_result['saml_response'])} chars)")
                        if "prelogin-cookie" in params:
                            saml_result["prelogin_cookie"] = params["prelogin-cookie"][0]
                            if debug:
                                print("    [DEBUG] Captured prelogin-cookie from POST")
                    except Exception as exc:
                        if debug:
                            print(f"    [DEBUG] Error parsing POST: {exc}")

        def handle_response(response):
            if not _is_vpn_url(response.url):
                return
            try:
                headers = response.headers
                if debug:
                    print(f"    [DEBUG] Response from VPN: {response.url[:80]}... status={response.status}")
                    for h in ["prelogin-cookie", "saml-username", "portal-userauthcookie", "set-cookie", "location"]:
                        if h in headers:
                            val = headers[h][:80] if len(headers[h]) > 80 else headers[h]
                            print(f"    [DEBUG] Header {h}: {val}...")
                if "prelogin-cookie" in headers:
                    saml_result["prelogin_cookie"] = headers["prelogin-cookie"]
                if "saml-username" in headers:
                    saml_result["saml_username"] = headers["saml-username"]
                if "portal-userauthcookie" in headers:
                    saml_result["portal_userauthcookie"] = headers["portal-userauthcookie"]
            except Exception:
                pass

        page.on("request", handle_request)
        page.on("response", handle_response)

        def _find_visible_in_frames(selectors: list[str]):
            for frame in page.frames:
                for sel in selectors:
                    loc = frame.locator(sel)
                    try:
                        if loc.count() > 0 and loc.first.is_visible():
                            return loc.first
                    except Exception:
                        continue
            return None

        def _click_first_text(texts: list[str]):
            for frame in page.frames:
                for t in texts:
                    loc = frame.get_by_text(t, exact=False)
                    try:
                        if loc.count() > 0 and loc.first.is_visible():
                            loc.first.click()
                            return True
                    except Exception:
                        continue
            return False

        def _click_first_selector(selectors: list[str]):
            for frame in page.frames:
                for sel in selectors:
                    loc = frame.locator(sel)
                    try:
                        if loc.count() > 0 and loc.first.is_visible():
                            loc.first.click()
                            return True
                    except Exception:
                        continue
            return False

        try:
            if protocol == "gp" and gp_saml_request:
                try:
                    start_url = base64.b64decode(gp_saml_request).decode("utf-8")
                    if not start_url.startswith("http"):
                        start_url = vpn_url
                except Exception:
                    start_url = vpn_url
            elif protocol == "anyconnect":
                start_url = f"https://{vpn_server_netloc}/+CSCOE+/saml/sp/login?tgname=DefaultWEBVPNGroup"
            else:
                start_url = vpn_url

            print("  [1/6] Opening SAML portal...")
            for attempt in range(3):
                try:
                    page.goto(start_url, timeout=30000, wait_until="networkidle")
                    break
                except Exception as exc:
                    if "ERR_NETWORK_CHANGED" in str(exc):
                        if debug:
                            print("    [DEBUG] Page.goto hit ERR_NETWORK_CHANGED; retrying")
                        time.sleep(1)
                        continue
                    raise

            if debug:
                page.screenshot(path="/tmp/vpn-step1-portal.png")
                print("    [DEBUG] Screenshot: /tmp/vpn-step1-portal.png")

            time.sleep(1)
            if _is_vpn_url(page.url):
                all_cookies = context.cookies()
                session_cookies = {}
                for c in all_cookies:
                    if c.get("value") and _cookie_domain_matches(c.get("domain", "")):
                        session_cookies[c["name"]] = c["value"]

                has_session = (
                    session_cookies.get("webvpn")
                    or session_cookies.get("SVPNCOOKIE")
                    or saml_result.get("saml_response")
                    or saml_result.get("prelogin_cookie")
                )
                if has_session:
                    print("  -> Already authenticated (SSO session valid)")
                    if saml_result["saml_response"]:
                        session_cookies["SAMLResponse"] = saml_result["saml_response"]
                    if saml_result["prelogin_cookie"]:
                        session_cookies["prelogin-cookie"] = saml_result["prelogin_cookie"]
                    if saml_result["portal_userauthcookie"]:
                        session_cookies["portal-userauthcookie"] = saml_result["portal_userauthcookie"]
                    if saml_result["saml_username"]:
                        session_cookies["saml-username"] = saml_result["saml_username"]
                    if gp_prelogin_cookie and "prelogin-cookie" not in session_cookies:
                        session_cookies["prelogin-cookie"] = gp_prelogin_cookie
                    if gp_gateway_ip:
                        session_cookies["_gateway_ip"] = gp_gateway_ip
                    context.close()
                    return session_cookies

            # Step 2: account selection
            _click_first_text(["Use another account", "Sign in with another account"])
            if username:
                account_tile = None
                for frame in page.frames:
                    try:
                        loc = frame.locator(f"text={username}")
                        if loc.count() > 0 and loc.first.is_visible():
                            account_tile = loc.first
                            break
                    except Exception:
                        continue
                if account_tile:
                    account_tile.click()

            # Step 3: username field
            user_loc = _find_visible_in_frames([
                "input[type='email']",
                "input[name='loginfmt']",
                "input[name='login']",
                "input[id='i0116']",
                "input[autocomplete='username']",
            ])
            if user_loc and username:
                user_loc.fill(username)
                _click_first_text(["Next", "Weiter", "Suivant", "Avanti", "Weiter >", "Continue"])

            # Step 4: password field
            pass_loc = _find_visible_in_frames([
                "input[type='password']",
                "input[name='passwd']",
                "input[id='i0118']",
                "input[autocomplete='current-password']",
            ])
            if pass_loc and password:
                pass_loc.fill(password)
                _click_first_text(["Sign in", "Anmelden", "Connexion", "Accedi", "Continue", "Next"])

            # Step 5: OTP / MFA
            if totp_secret and auto_totp:
                otp_loc = _find_visible_in_frames([
                    "input[name='otc']",
                    "input[id='otc']",
                    "input[name='code']",
                    "input[type='tel']",
                    "input[autocomplete='one-time-code']",
                ])
                if otp_loc:
                    otp_loc.fill(generate_totp(totp_secret))
                    _click_first_text(["Verify", "Überprüfen", "Continue", "Next", "Anmelden"])

            # Try "Send notification" / MFA prompt
            _click_first_text(["Send notification", "Send push", "Approve", "Verify", "Continue"])

            # "Use your password instead" fallback
            _click_first_text(["Use your password instead", "Use password instead"])
            _click_first_text(["Sign in", "Continue", "Next"])

            # "Stay signed in?" prompt
            _click_first_text(["Yes", "No", "Stay signed in?"])
            _click_first_selector(["input[id='idSIButton9']", "button#idSIButton9"])

            _wait_for_vpn_callback(90000)

            # Collect cookies
            all_cookies = context.cookies()
            vpn_cookies = {}
            for c in all_cookies:
                if c.get("value") and _cookie_domain_matches(c.get("domain", "")):
                    vpn_cookies[c["name"]] = c["value"]

            if saml_result["saml_response"]:
                vpn_cookies["SAMLResponse"] = saml_result["saml_response"]
            if saml_result["prelogin_cookie"]:
                vpn_cookies["prelogin-cookie"] = saml_result["prelogin_cookie"]
            if saml_result["portal_userauthcookie"]:
                vpn_cookies["portal-userauthcookie"] = saml_result["portal_userauthcookie"]
            if saml_result["saml_username"]:
                vpn_cookies["saml-username"] = saml_result["saml_username"]
            if gp_prelogin_cookie and "prelogin-cookie" not in vpn_cookies:
                vpn_cookies["prelogin-cookie"] = gp_prelogin_cookie
            if gp_gateway_ip:
                vpn_cookies["_gateway_ip"] = gp_gateway_ip

            if debug:
                debug_out = {
                    "vpn_server": vpn_server,
                    "vpn_server_host": vpn_server_host,
                    "vpn_server_netloc": vpn_server_netloc,
                    "vpn_server_ip": vpn_server_ip,
                    "final_url": page.url,
                    "cookies": list(vpn_cookies.keys()),
                    "cookie_domains": sorted({c.get("domain", "") for c in all_cookies}),
                    "saml_response": bool(saml_result["saml_response"]),
                    "prelogin_cookie": bool(vpn_cookies.get("prelogin-cookie")),
                }
                try:
                    with open("/tmp/nm-vpn-auth-debug.json", "w") as f:
                        json.dump(debug_out, f, indent=2)
                except Exception:
                    pass

            context.close()
            return vpn_cookies
        except Exception:
            if debug:
                try:
                    page.screenshot(path="/tmp/vpn-auth-error.png")
                except Exception:
                    pass
            context.close()
            raise
