#!/usr/bin/env python3
"""Headless/GUI SAML authentication helper for GNOME VPN SSO."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from core.auth import do_saml_auth  # noqa: E402


def _build_cookie(protocol: str, cookies: dict) -> tuple[str | None, str | None]:
    ignored = {"_gateway_ip", "saml-username"}

    if protocol == "gp":
        if "portal-userauthcookie" in cookies:
            return cookies["portal-userauthcookie"], "portal:portal-userauthcookie"
        if "prelogin-cookie" in cookies:
            return cookies["prelogin-cookie"], "portal:prelogin-cookie"
        if "SAMLResponse" in cookies:
            return cookies["SAMLResponse"], "portal:prelogin-cookie"
        if "SESSID" in cookies:
            return cookies["SESSID"], "portal:portal-userauthcookie"
        combined = "; ".join([f"{k}={v}" for k, v in cookies.items() if k not in ignored])
        return (combined if combined else None), "portal:portal-userauthcookie"

    combined = "; ".join([f"{k}={v}" for k, v in cookies.items() if k not in ignored])
    return (combined if combined else None), None


def main() -> int:
    parser = argparse.ArgumentParser(description="GNOME VPN SSO SAML auth helper")
    parser.add_argument("--protocol", required=True, help="anyconnect|gp|globalprotect")
    parser.add_argument("--gateway", required=True, help="VPN gateway hostname")
    parser.add_argument("--username", help="Username (optional)")
    parser.add_argument("--password", help="Password (optional)")
    parser.add_argument("--totp-secret", help="TOTP secret (optional)")
    parser.add_argument("--headless", action="store_true", help="Run browser headless")
    parser.add_argument("--headful", action="store_true", help="Run browser with UI")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    parser.add_argument("--no-auto-totp", action="store_true", help="Disable auto TOTP")

    args = parser.parse_args()

    protocol = args.protocol.lower().strip()
    if protocol in ("globalprotect", "gp"):
        protocol = "gp"
    elif protocol in ("anyconnect", "ac"):
        protocol = "anyconnect"
    else:
        print(f"ERROR: Unknown protocol '{args.protocol}'", file=sys.stderr)
        return 2

    username = args.username or os.environ.get("VPN_SSO_USERNAME") or ""
    password = args.password or os.environ.get("VPN_SSO_PASSWORD") or ""
    totp_secret = args.totp_secret or os.environ.get("VPN_SSO_TOTP_SECRET")

    if args.headful:
        headless = False
    elif args.headless:
        headless = True
    else:
        headless = bool(password or totp_secret)

    try:
        cookies = do_saml_auth(
            vpn_server=args.gateway,
            username=username,
            password=password,
            totp_secret=totp_secret,
            protocol=protocol,
            auto_totp=not args.no_auto_totp,
            headless=headless,
            debug=args.debug,
        )
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if not cookies:
        print("ERROR: SAML authentication returned no cookies", file=sys.stderr)
        return 1

    cookie_value, usergroup = _build_cookie(protocol, cookies)
    if not cookie_value:
        print("ERROR: No valid auth cookie extracted", file=sys.stderr)
        return 1

    if protocol == "gp" and usergroup:
        print(f"USERGROUP={usergroup}")

    if not username:
        username = cookies.get("saml-username", "")
    if username:
        print(f"USERNAME={username}")

    print(f"COOKIE={cookie_value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
