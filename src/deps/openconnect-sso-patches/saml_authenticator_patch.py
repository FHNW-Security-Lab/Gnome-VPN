"""
Patched saml_authenticator.py for openconnect-sso with external browser support.
"""

import structlog

from openconnect_sso.browser import Browser

log = structlog.get_logger()


# PATCH: Add external_browser parameter
async def authenticate_in_browser(proxy, auth_info, credentials, display_mode, external_browser=False):
    if external_browser:
        # Use external browser (Chrome/Chromium with CDP)
        log.info("Using external browser for SSO authentication")
        try:
            from openconnect_sso.external_browser import authenticate_in_browser_external
            return await authenticate_in_browser_external(proxy, auth_info, credentials, display_mode)
        except ImportError as e:
            log.error("External browser module not available, falling back to embedded browser", error=str(e))
        except Exception as e:
            log.error("External browser authentication failed, falling back to embedded browser", error=str(e))

    # Original implementation using embedded Qt WebEngine browser
    async with Browser(proxy, display_mode) as browser:
        await browser.authenticate_at(auth_info.login_url, credentials)

        while browser.url != auth_info.login_final_url:
            await browser.page_loaded()
            log.debug("Browser loaded page", url=browser.url)

    return browser.cookies[auth_info.token_cookie_name]
