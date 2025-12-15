#!/usr/bin/env python3
"""
External browser authentication support for openconnect-sso.

This module provides an alternative browser implementation that opens
the SSO URL in the system's default browser (Chrome/Chromium with CDP
or Firefox) and captures the authentication token.
"""

import asyncio
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import webbrowser
from pathlib import Path

import structlog

logger = structlog.get_logger()

# Try to import websockets for CDP
try:
    import websockets
    HAS_WEBSOCKETS = True
except ImportError:
    HAS_WEBSOCKETS = False


class ExternalBrowser:
    """
    External browser implementation for SSO authentication.

    Uses Chrome DevTools Protocol (CDP) to control Chrome/Chromium
    with remote debugging, allowing password managers to work while
    still capturing the authentication token.
    """

    def __init__(self, proxy=None, display_mode=None):
        self.proxy = proxy
        self.display_mode = display_mode
        self.process = None
        self.cdp_ws = None
        self.temp_profile = None
        self.url = None
        self.cookies = {}
        self._msg_id = 0
        self._final_url = None
        self._token_cookie_name = None

    def _find_chrome(self):
        """Find Chrome/Chromium executable."""
        candidates = [
            "google-chrome",
            "google-chrome-stable",
            "chromium",
            "chromium-browser",
            "/usr/bin/google-chrome",
            "/usr/bin/chromium",
            "/usr/bin/chromium-browser",
            "/snap/bin/chromium",
        ]
        for cmd in candidates:
            path = shutil.which(cmd)
            if path:
                return path
        return None

    async def __aenter__(self):
        """Start browser with remote debugging."""
        chrome_path = self._find_chrome()
        if not chrome_path:
            logger.warning("Chrome/Chromium not found, falling back to manual mode")
            return self

        # Create temporary profile directory
        self.temp_profile = tempfile.mkdtemp(prefix="vpn-sso-browser-")

        # Find a free debugging port
        debug_port = 9222
        for port in range(9222, 9300):
            try:
                import socket
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.bind(('127.0.0.1', port))
                s.close()
                debug_port = port
                break
            except OSError:
                continue

        # Launch Chrome with remote debugging
        args = [
            chrome_path,
            f"--remote-debugging-port={debug_port}",
            f"--user-data-dir={self.temp_profile}",
            "--no-first-run",
            "--no-default-browser-check",
            "--disable-background-networking",
            "--disable-client-side-phishing-detection",
            "--disable-default-apps",
            "--disable-hang-monitor",
            "--disable-popup-blocking",
            "--disable-prompt-on-repost",
            "--disable-sync",
            "--disable-translate",
            "--metrics-recording-only",
            "--safebrowsing-disable-auto-update",
            "--password-store=basic",  # Allow password manager extensions
        ]

        if self.proxy:
            args.append(f"--proxy-server={self.proxy}")

        try:
            self.process = subprocess.Popen(
                args,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            logger.info("Started Chrome with remote debugging", port=debug_port, pid=self.process.pid)

            # Wait for Chrome to start
            await asyncio.sleep(1)

            # Connect to CDP
            if HAS_WEBSOCKETS:
                await self._connect_cdp(debug_port)

        except Exception as e:
            logger.error("Failed to start Chrome", error=str(e))
            self.process = None

        return self

    async def _connect_cdp(self, port):
        """Connect to Chrome DevTools Protocol."""
        import aiohttp

        # Get WebSocket URL from Chrome
        for _ in range(30):  # Try for 3 seconds
            try:
                async with aiohttp.ClientSession() as session:
                    async with session.get(f"http://127.0.0.1:{port}/json/version") as resp:
                        data = await resp.json()
                        ws_url = data.get("webSocketDebuggerUrl")
                        if ws_url:
                            self.cdp_ws = await websockets.connect(ws_url)
                            logger.info("Connected to Chrome DevTools Protocol")
                            return
            except Exception:
                await asyncio.sleep(0.1)

        logger.warning("Could not connect to Chrome DevTools Protocol")

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Clean up browser."""
        if self.cdp_ws:
            await self.cdp_ws.close()

        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()

        if self.temp_profile and os.path.exists(self.temp_profile):
            import shutil
            try:
                shutil.rmtree(self.temp_profile)
            except Exception:
                pass

    async def _cdp_send(self, method, params=None):
        """Send a CDP command and wait for response."""
        if not self.cdp_ws:
            return None

        self._msg_id += 1
        msg = {"id": self._msg_id, "method": method}
        if params:
            msg["params"] = params

        await self.cdp_ws.send(json.dumps(msg))

        # Wait for response
        while True:
            resp_str = await self.cdp_ws.recv()
            resp = json.loads(resp_str)
            if resp.get("id") == self._msg_id:
                return resp.get("result", {})

    async def authenticate_at(self, url, credentials):
        """Navigate to authentication URL."""
        self._url = url

        if self.cdp_ws:
            # Use CDP to navigate
            await self._cdp_send("Page.navigate", {"url": url})
            logger.info("Navigated to SSO URL via CDP")
        elif self.process:
            # Chrome is running but CDP not connected, open URL in it
            webbrowser.open(url)
            logger.info("Opened SSO URL in browser")
        else:
            # Fallback to default browser
            webbrowser.open(url)
            logger.info("Opened SSO URL in default browser")

        print("\n" + "=" * 60)
        print("EXTERNAL BROWSER AUTHENTICATION")
        print("=" * 60)
        print("\nA browser window has been opened for SSO login.")
        print("Please complete the authentication in your browser.")
        print("\n✓ Password manager extensions should work!")
        print("\nWaiting for authentication to complete...")
        print("=" * 60 + "\n")

    async def page_loaded(self):
        """Wait for page load and check URL."""
        if self.cdp_ws:
            # Use CDP to monitor page loads
            await self._cdp_send("Page.enable")

            # Wait for page load events
            while True:
                try:
                    msg_str = await asyncio.wait_for(self.cdp_ws.recv(), timeout=1.0)
                    msg = json.loads(msg_str)

                    if msg.get("method") == "Page.frameNavigated":
                        frame = msg.get("params", {}).get("frame", {})
                        self.url = frame.get("url", "")
                        logger.debug("Page navigated", url=self.url[:60])

                        if self.url and self._final_url and self.url.startswith(self._final_url):
                            # We reached the final URL, grab cookies
                            await self._get_cookies()
                            return

                except asyncio.TimeoutError:
                    # Check if we're at the final URL
                    result = await self._cdp_send("Page.getNavigationHistory")
                    if result:
                        entries = result.get("entries", [])
                        if entries:
                            current = entries[-1].get("url", "")
                            if current and self._final_url and current.startswith(self._final_url):
                                self.url = current
                                await self._get_cookies()
                                return
        else:
            # Manual mode - ask user to paste token
            await asyncio.sleep(0.5)  # Brief pause

    async def _get_cookies(self):
        """Get cookies from browser via CDP."""
        if not self.cdp_ws:
            return

        result = await self._cdp_send("Network.getAllCookies")
        if result:
            for cookie in result.get("cookies", []):
                name = cookie.get("name")
                value = cookie.get("value")
                if name:
                    self.cookies[name] = value
                    logger.debug("Got cookie", name=name)


async def authenticate_in_browser_external(proxy, auth_info, credentials, display_mode):
    """
    External browser authentication - opens system browser for SSO.

    This allows password manager extensions to work during authentication.
    """

    async with ExternalBrowser(proxy, display_mode) as browser:
        browser._final_url = auth_info.login_final_url
        browser._token_cookie_name = auth_info.token_cookie_name

        await browser.authenticate_at(auth_info.login_url, credentials)

        # Wait for user to complete authentication
        timeout = 300  # 5 minutes
        start_time = time.time()

        while time.time() - start_time < timeout:
            await browser.page_loaded()

            # Check if we have the token
            if browser._token_cookie_name in browser.cookies:
                token = browser.cookies[browser._token_cookie_name]
                logger.info("Got authentication token from external browser")
                return token

            # Check if we reached the final URL
            if browser.url and auth_info.login_final_url:
                if browser.url.startswith(auth_info.login_final_url):
                    if browser._token_cookie_name in browser.cookies:
                        return browser.cookies[browser._token_cookie_name]

            await asyncio.sleep(0.5)

        # Timeout - ask for manual entry
        print("\n" + "=" * 60)
        print("MANUAL TOKEN ENTRY REQUIRED")
        print("=" * 60)
        print(f"\nCould not automatically capture the authentication token.")
        print(f"\nPlease follow these steps:")
        print(f"1. In your browser, open Developer Tools (F12)")
        print(f"2. Go to Application tab -> Storage -> Cookies")
        print(f"3. Find the cookie named: {auth_info.token_cookie_name}")
        print(f"4. Copy its value and paste below")
        print("=" * 60 + "\n")

        try:
            token = input(f"Paste {auth_info.token_cookie_name} value: ").strip()
            if token:
                return token
        except (EOFError, KeyboardInterrupt):
            pass

        raise RuntimeError("No authentication token received")
