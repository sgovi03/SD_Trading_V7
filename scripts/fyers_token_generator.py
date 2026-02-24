#!/usr/bin/env python3
"""
Fyers OAuth Token Generator
Handles the complete OAuth flow and generates access_token
"""

import hashlib
import json
import webbrowser
import requests
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import sys

# ============================================================
# CONFIGURATION - UPDATE THESE VALUES
# ============================================================

CLIENT_ID = "CCJOKAUTF6-100"  # Your Fyers client_id (api_id + "-100")
APP_SECRET = "A2J9ADLH9N"  # Your Fyers app_secret
REDIRECT_URI = "http://127.0.0.1:5000/callback"  # Must match Fyers dashboard
API_BASE_URL = "https://api-t1.fyers.in/api/v3"  # Use api-t1 for testing, api.fyers.in for production

# ============================================================
# OAuth Flow Implementation
# ============================================================

class CallbackHandler(BaseHTTPRequestHandler):
    """HTTP server to catch the OAuth callback"""
    
    auth_code = None
    
    def do_GET(self):
        """Handle the redirect callback"""
        query = urlparse(self.path).query
        params = parse_qs(query)
        
        if 'auth_code' in params:
            CallbackHandler.auth_code = params['auth_code'][0]
            
            # Send success response to browser
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(b"""
                <html>
                <body style="font-family: Arial; text-align: center; margin-top: 50px;">
                    <h1 style="color: green;">✓ Authentication Successful!</h1>
                    <p>You can close this window and return to the terminal.</p>
                    <p>Auth code received successfully.</p>
                </body>
                </html>
            """)
        else:
            self.send_response(400)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(b"""
                <html>
                <body style="font-family: Arial; text-align: center; margin-top: 50px;">
                    <h1 style="color: red;">✗ Authentication Failed</h1>
                    <p>No auth_code received in callback.</p>
                </body>
                </html>
            """)
    
    def log_message(self, format, *args):
        """Suppress request logging"""
        pass


def generate_app_id_hash(client_id, app_secret):
    """
    Generate appIdHash: SHA-256 of "api_id:app_secret"
    
    Args:
        client_id: Fyers client_id (e.g., "CCJOKAUTF6-100")
        app_secret: Fyers app_secret
    
    Returns:
        SHA-256 hash as hex string
    """
    # Extract api_id from client_id (remove "-100")
    api_id = client_id.split('-')[0]
    
    # Concatenate with colon separator
    input_string = f"{api_id}:{app_secret}"
    print(f"🔐 Generating hash for: {api_id}:{'*' * len(app_secret)}")
    
    # Calculate SHA-256
    hash_bytes = hashlib.sha256(input_string.encode('utf-8')).digest()
    hash_hex = hash_bytes.hex()
    
    print(f"✅ AppIdHash: {hash_hex[:16]}...{hash_hex[-16:]}")
    return hash_hex


def get_auth_code(client_id, redirect_uri):
    """
    Step 1: Get authorization code via browser
    
    Args:
        client_id: Fyers client_id
        redirect_uri: Callback URL
    
    Returns:
        auth_code string
    """
    print("\n" + "="*60)
    print("STEP 1: Getting Authorization Code")
    print("="*60)
    
    # Build authorization URL
    state = "fyers_oauth_state_123"
    auth_url = (
        f"{API_BASE_URL}/generate-authcode"
        f"?client_id={client_id}"
        f"&redirect_uri={redirect_uri}"
        f"&response_type=code"
        f"&state={state}"
    )
    
    print(f"\n🌐 Opening browser for Fyers login...")
    print(f"📡 URL: {auth_url}\n")
    
    # Start local server to catch callback
    server = HTTPServer(('127.0.0.1', 5000), CallbackHandler)
    
    # Open browser
    webbrowser.open(auth_url)
    
    print("⏳ Waiting for authentication...")
    print("   Please login in the browser window that opened.\n")
    
    # Wait for single request (the callback)
    server.handle_request()
    
    if CallbackHandler.auth_code:
        print(f"✅ Auth code received: {CallbackHandler.auth_code[:20]}...\n")
        return CallbackHandler.auth_code
    else:
        print("❌ Failed to receive auth code\n")
        return None


def exchange_auth_code_for_token(auth_code, app_id_hash):
    """
    Step 2: Exchange auth_code for access_token
    
    Args:
        auth_code: Authorization code from Step 1
        app_id_hash: SHA-256 hash of api_id:app_secret
    
    Returns:
        dict with access_token and refresh_token
    """
    print("="*60)
    print("STEP 2: Exchanging Auth Code for Access Token")
    print("="*60 + "\n")
    
    url = f"{API_BASE_URL}/validate-authcode"
    
    payload = {
        "grant_type": "authorization_code",
        "appIdHash": app_id_hash,
        "code": auth_code
    }
    
    headers = {
        "Content-Type": "application/json"
    }
    
    print(f"📡 POST {url}")
    print(f"📦 Payload: grant_type=authorization_code, code={auth_code[:20]}...\n")
    print("⏳ Requesting access token...\n")
    
    try:
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        
        print(f"📊 HTTP Status: {response.status_code}")
        print(f"📄 Response: {response.text}\n")
        
        if response.status_code == 200:
            data = response.json()
            
            if 's' in data and data['s'] == 'ok':
                print("✅ Token exchange successful!\n")
                return {
                    'access_token': data.get('access_token'),
                    'refresh_token': data.get('refresh_token')
                }
            else:
                print(f"❌ API returned error: {data.get('message', 'Unknown error')}\n")
                return None
        else:
            print(f"❌ HTTP {response.status_code}: {response.text}\n")
            return None
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}\n")
        return None


def save_tokens(access_token, refresh_token):
    """
    Save tokens to file for use by C++ application
    
    Args:
        access_token: Fyers access token
        refresh_token: Fyers refresh token
    """
    print("="*60)
    print("STEP 3: Saving Tokens")
    print("="*60 + "\n")
    
    # Save to JSON file
    tokens = {
        "access_token": access_token,
        "refresh_token": refresh_token
    }
    
    with open("fyers_token.json", "w") as f:
        json.dump(tokens, f, indent=2)
    
    print(f"✅ Tokens saved to: fyers_token.json\n")
    
    # Also save just access token to text file for easy command line use
    with open("fyers_token.txt", "w") as f:
        f.write(access_token)
    
    print(f"✅ Access token saved to: fyers_token.txt\n")
    
    print("Token details:")
    print(f"  Access Token:  {access_token[:30]}...{access_token[-30:]}")
    print(f"  Refresh Token: {refresh_token[:30]}...{refresh_token[-30:]}\n")


def main():
    """Main OAuth flow"""
    print("\n" + "🚀"*30)
    print("      FYERS TOKEN GENERATOR")
    print("🚀"*30 + "\n")
    
    # Validate configuration
    if APP_SECRET == "YOUR_APP_SECRET_HERE":
        print("❌ ERROR: Please update APP_SECRET in this script!")
        print("\nEdit this file and set:")
        print(f'  CLIENT_ID = "{CLIENT_ID}"')
        print('  APP_SECRET = "your_actual_secret_from_fyers_dashboard"')
        print(f'  REDIRECT_URI = "{REDIRECT_URI}"\n')
        sys.exit(1)
    
    print(f"📋 Configuration:")
    print(f"   Client ID: {CLIENT_ID}")
    print(f"   Redirect URI: {REDIRECT_URI}")
    print(f"   API Base: {API_BASE_URL}\n")
    
    # Step 1: Get auth code
    auth_code = get_auth_code(CLIENT_ID, REDIRECT_URI)
    if not auth_code:
        print("❌ Failed to get authorization code. Exiting.")
        sys.exit(1)
    
    # Generate app ID hash
    app_id_hash = generate_app_id_hash(CLIENT_ID, APP_SECRET)
    
    # Step 2: Exchange for access token
    tokens = exchange_auth_code_for_token(auth_code, app_id_hash)
    if not tokens:
        print("❌ Failed to get access token. Exiting.")
        sys.exit(1)
    
    # Step 3: Save tokens
    save_tokens(tokens['access_token'], tokens['refresh_token'])
    
    print("="*60)
    print("✅ SUCCESS! You can now use the trading application")
    print("="*60 + "\n")
    
    print("Next steps:")
    print("  1. Run: sd_realtime_trading.exe <access_token> <symbol>")
    print("  2. Or use: run_paper_trading.bat (reads token automatically)\n")
    
    print("🎉 Token generation complete!\n")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n⚠️  Interrupted by user. Exiting.\n")
        sys.exit(0)
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}\n")
        import traceback
        traceback.print_exc()
        sys.exit(1)
