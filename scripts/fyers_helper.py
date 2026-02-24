#!/usr/bin/env python3
"""
FYERS API HELPER - WITH REFRESH TOKEN SUPPORT
=============================================
Implements automatic token refresh using refresh tokens (15-day validity)
"""

import os
import sys
import json
import time
import hashlib
from datetime import datetime, timedelta
from pathlib import Path
import requests

try:
    from fyers_apiv3 import fyersModel
except ImportError:
    print("❌ Missing dependencies. Install with:")
    print("   pip3 install fyers-apiv3 requests")
    sys.exit(1)

# Import SystemConfig
from system_config import load_config

# ============================================================
# LOAD CONFIGURATION
# ============================================================

try:
    system_config = load_config()
    
    # Get Fyers configuration
    CLIENT_ID = system_config.get_value('fyers', 'client_id')
    SECRET_KEY = system_config.get_value('fyers', 'secret_key')
    REDIRECT_URI = system_config.get_value('fyers', 'redirect_uri', 
                                           default='https://trade.fyers.in/api-login/redirect-uri/index.html')
    
    # Get file paths
    TOKEN_FILE = system_config.get_path('fyers_token')
    REFRESH_TOKEN_FILE = TOKEN_FILE.parent / 'fyers_refresh_token.txt'
    TOKEN_METADATA_FILE = TOKEN_FILE.parent / 'fyers_token_metadata.json'
    
    # Get API endpoint
    API_BASE_URL = system_config.get_value('fyers', 'api_base')
    
    print(f"✅ Configuration loaded")
    print(f"   Client ID: {CLIENT_ID}")
    print(f"   Secret Key: {SECRET_KEY[:10]}..." if SECRET_KEY else "   Secret Key: NOT SET")
    print(f"   API Base: {API_BASE_URL}")
    print(f"   Token File: {TOKEN_FILE}")
    print(f"   Refresh Token File: {REFRESH_TOKEN_FILE}\n")
    
    # Validate configuration
    if not SECRET_KEY or SECRET_KEY == "YOUR_SECRET_KEY_HERE":
        print("❌ ERROR: Secret key not configured!")
        print("\nPlease update system_config.json and add your Fyers secret key")
        sys.exit(1)
    
except Exception as e:
    print(f"[ERROR] Failed to load system configuration: {e}")
    sys.exit(1)


# ============================================================
# HELPER FUNCTIONS
# ============================================================

def generate_app_id_hash():
    """Generate SHA-256 hash of client_id:secret_key"""
    hash_string = f"{CLIENT_ID}:{SECRET_KEY}"
    return hashlib.sha256(hash_string.encode()).hexdigest()


def save_token_metadata(access_token, refresh_token=None, pin=None):
    """Save token metadata including expiry times"""
    metadata = {
        'access_token_generated': datetime.now().isoformat(),
        'access_token_expires': (datetime.now() + timedelta(hours=23)).isoformat(),
        'access_token': access_token[:50] + '...',  # Save partial for reference
    }
    
    if refresh_token:
        metadata['refresh_token_generated'] = datetime.now().isoformat()
        metadata['refresh_token_expires'] = (datetime.now() + timedelta(days=15)).isoformat()
        metadata['refresh_token'] = refresh_token[:50] + '...'
    
    if pin:
        metadata['pin_saved'] = True
    
    with open(TOKEN_METADATA_FILE, 'w') as f:
        json.dump(metadata, f, indent=2)
    
    print(f"💾 Saved token metadata to {TOKEN_METADATA_FILE}")


def load_token_metadata():
    """Load token metadata"""
    if not TOKEN_METADATA_FILE.exists():
        return None
    
    try:
        with open(TOKEN_METADATA_FILE) as f:
            return json.load(f)
    except:
        return None


def is_access_token_expired():
    """Check if access token is expired"""
    metadata = load_token_metadata()
    if not metadata or 'access_token_expires' not in metadata:
        return True
    
    expiry = datetime.fromisoformat(metadata['access_token_expires'])
    # Consider expired if less than 1 hour remaining
    return datetime.now() >= (expiry - timedelta(hours=1))


def is_refresh_token_expired():
    """Check if refresh token is expired"""
    metadata = load_token_metadata()
    if not metadata or 'refresh_token_expires' not in metadata:
        return True
    
    expiry = datetime.fromisoformat(metadata['refresh_token_expires'])
    # Consider expired if less than 1 day remaining
    return datetime.now() >= (expiry - timedelta(days=1))


def save_refresh_token(refresh_token):
    """Save refresh token to file"""
    REFRESH_TOKEN_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(REFRESH_TOKEN_FILE, 'w') as f:
        f.write(refresh_token)
    print(f"✅ Refresh token saved to {REFRESH_TOKEN_FILE}")


def load_refresh_token():
    """Load refresh token from file"""
    if not REFRESH_TOKEN_FILE.exists():
        return None
    
    with open(REFRESH_TOKEN_FILE) as f:
        return f.read().strip()


def save_pin(pin):
    """Save user PIN (encrypted would be better in production)"""
    pin_file = TOKEN_FILE.parent / '.fyers_pin'
    with open(pin_file, 'w') as f:
        f.write(pin)
    
    # Set restrictive permissions on Unix-like systems
    try:
        os.chmod(pin_file, 0o600)
    except:
        pass
    
    return pin_file


def load_pin():
    """Load saved PIN"""
    pin_file = TOKEN_FILE.parent / '.fyers_pin'
    if not pin_file.exists():
        return None
    
    with open(pin_file) as f:
        return f.read().strip()


# ============================================================
# TOKEN GENERATION (INITIAL SETUP)
# ============================================================

def generate_access_token_initial():
    """
    Generate access token with OAuth flow (first time setup)
    Also receives refresh token
    """
    print("=" * 60)
    print("FYERS API - INITIAL TOKEN GENERATION")
    print("=" * 60)
    print()
    
    # Get PIN
    pin = input("📝 Enter your Fyers PIN (will be saved securely): ").strip()
    if not pin:
        print("❌ PIN is required!")
        return None
    
    save_pin(pin)
    print("✅ PIN saved\n")
    
    # Step 1: Generate auth code URL
    print("STEP 1: Getting Authorization Code")
    print("=" * 60)
    
    session = fyersModel.SessionModel(
        client_id=CLIENT_ID,
        secret_key=SECRET_KEY,
        redirect_uri=REDIRECT_URI,
        response_type="code",
        grant_type="authorization_code"
    )
    
    auth_url = session.generate_authcode()
    print("\n🔗 Authorization URL:")
    print(auth_url)
    print()
    print("📋 Instructions:")
    print("   1. Copy the URL above")
    print("   2. Paste it in your browser")
    print("   3. Log in to Fyers")
    print("   4. After login, you'll be redirected to a URL")
    print("   5. Copy the auth_code from the URL")
    print()
    
    # Step 2: Get auth code from user
    auth_code = input("📝 Paste the auth_code: ").strip()
    
    if not auth_code:
        print("❌ No auth code provided!")
        return None
    
    # Remove URL prefix if user pasted whole URL
    if "auth_code=" in auth_code:
        auth_code = auth_code.split("auth_code=")[1].split("&")[0]
        print(f"   Extracted auth_code: {auth_code[:50]}...")
    
    print()
    print("STEP 2: Generating Access Token & Refresh Token")
    print("=" * 60)
    
    # Step 3: Exchange auth code for tokens
    session.set_token(auth_code)
    
    try:
        response = session.generate_token()
        print(f"\n✅ API Response: {response}")
        
        if response.get('code') == 200:
            access_token = response['access_token']
            refresh_token = response.get('refresh_token')
            
            # Save access token
            TOKEN_FILE.parent.mkdir(parents=True, exist_ok=True)
            with open(TOKEN_FILE, 'w') as f:
                f.write(access_token)
            
            print(f"\n✅ Access token generated!")
            print(f"💾 Saved to: {TOKEN_FILE}")
            print(f"\n🔑 Token (first 50 chars): {access_token[:50]}...")
            
            # Save refresh token if available
            if refresh_token:
                save_refresh_token(refresh_token)
                print(f"\n🔄 Refresh token received!")
                print(f"💾 Saved to: {REFRESH_TOKEN_FILE}")
                print(f"⏰ Valid for: 15 days")
                print(f"\n🔑 Refresh Token (first 50 chars): {refresh_token[:50]}...")
            else:
                print("\n⚠️ Warning: No refresh token in response")
                print("   You may need to regenerate tokens after 24 hours")
            
            # Save metadata
            save_token_metadata(access_token, refresh_token, pin)
            
            # Validate token
            print("\n" + "=" * 60)
            print("STEP 3: Validating Access Token")
            print("=" * 60)
            validate_token_with_sdk(access_token)
            
            return access_token
        else:
            print(f"\n❌ Failed to generate token")
            print(f"Response: {response}")
            print("\nCommon issues:")
            print("   - Auth code expired (regenerate URL)")
            print("   - Wrong secret key in system_config.json")
            print("   - Auth code already used (get a new one)")
            return None
            
    except Exception as e:
        print(f"\n❌ Error generating token: {e}")
        return None


# ============================================================
# REFRESH TOKEN USAGE (AUTOMATIC DAILY)
# ============================================================

def refresh_access_token():
    """
    Generate new access token using refresh token
    This should be called daily (automatic)
    """
    print("=" * 60)
    print("FYERS API - REFRESHING ACCESS TOKEN")
    print("=" * 60)
    print()
    
    # Load refresh token
    refresh_token = load_refresh_token()
    if not refresh_token:
        print("❌ No refresh token found!")
        print("   Please run initial token generation first")
        print("   Option: 1. Generate Access Token (Initial Setup)")
        return None
    
    # Load PIN
    pin = load_pin()
    if not pin:
        pin = input("📝 Enter your Fyers PIN: ").strip()
        if not pin:
            print("❌ PIN is required!")
            return None
        save_pin(pin)
    
    # Generate app ID hash
    app_id_hash = generate_app_id_hash()
    
    print(f"🔄 Using refresh token: {refresh_token[:50]}...")
    print(f"🔑 App ID Hash: {app_id_hash[:50]}...")
    print()
    
    # Prepare request
    url = f"{API_BASE_URL}/api/v3/validate-refresh-token"
    headers = {
        'Content-Type': 'application/json'
    }
    payload = {
        "grant_type": "refresh_token",
        "appIdHash": app_id_hash,
        "refresh_token": refresh_token,
        "pin": pin
    }
    
    try:
        print(f"📡 POST {url}")
        response = requests.post(url, headers=headers, json=payload, timeout=10)
        data = response.json()
        
        print(f"✅ Response: {data}")
        
        if data.get('s') == 'ok' and data.get('code') == 200:
            access_token = data['access_token']
            
            # Save new access token
            with open(TOKEN_FILE, 'w') as f:
                f.write(access_token)
            
            print(f"\n✅ New access token generated!")
            print(f"💾 Saved to: {TOKEN_FILE}")
            print(f"\n🔑 Token (first 50 chars): {access_token[:50]}...")
            
            # Update metadata (preserve refresh token info)
            # Load existing refresh token since it's not returned in refresh response
            existing_refresh_token = load_refresh_token()
            save_token_metadata(access_token, existing_refresh_token, pin)
            
            # Validate new token
            print("\n" + "=" * 60)
            print("Validating New Access Token")
            print("=" * 60)
            validate_token_with_sdk(access_token)
            
            return access_token
        else:
            print(f"\n❌ Failed to refresh token")
            print(f"Response: {data}")
            
            if data.get('code') == -5:
                print("\n⚠️ Refresh token may be expired or invalid")
                print("   Please run initial token generation again")
                print("   Option: 1. Generate Access Token (Initial Setup)")
            
            return None
            
    except Exception as e:
        print(f"\n❌ Error refreshing token: {e}")
        return None


# ============================================================
# AUTOMATIC TOKEN MANAGEMENT
# ============================================================

def get_valid_access_token():
    """
    Get a valid access token (auto-refresh if needed)
    This is the function to call from fyers_bridge.py
    """
    print("🔍 Checking token status...")
    
    # Check if access token exists and is valid
    if TOKEN_FILE.exists() and not is_access_token_expired():
        with open(TOKEN_FILE) as f:
            access_token = f.read().strip()
        
        print("✅ Existing access token is still valid")
        print(f"   Token: {access_token[:50]}...")
        
        metadata = load_token_metadata()
        if metadata and 'access_token_expires' in metadata:
            expiry = datetime.fromisoformat(metadata['access_token_expires'])
            remaining = expiry - datetime.now()
            hours = remaining.total_seconds() / 3600
            print(f"   Expires in: {hours:.1f} hours")
        
        return access_token
    
    print("⚠️ Access token expired or missing")
    
    # Check if refresh token is valid
    if not is_refresh_token_expired():
        print("🔄 Refresh token is valid - attempting auto-refresh...")
        access_token = refresh_access_token()
        if access_token:
            return access_token
    else:
        print("❌ Refresh token expired or missing")
    
    # Need manual setup
    print("\n" + "=" * 60)
    print("⚠️ MANUAL TOKEN GENERATION REQUIRED")
    print("=" * 60)
    print("\nPlease run: python fyers_helper.py")
    print("And select option 1 (Initial Token Generation)")
    
    return None


# ============================================================
# TOKEN VALIDATION
# ============================================================

def validate_token_with_sdk(access_token=None):
    """Validate token using Fyers SDK"""
    if not access_token:
        if not TOKEN_FILE.exists():
            print(f"❌ Token file not found: {TOKEN_FILE}")
            return False
        
        with open(TOKEN_FILE) as f:
            access_token = f.read().strip()
    
    try:
        fyers = fyersModel.FyersModel(
            client_id=CLIENT_ID,
            is_async=False,
            token=access_token,
            log_path=""
        )
        
        response = fyers.get_profile()
        
        if response.get('s') == 'ok':
            print(f"✅ Token is valid!")
            print(f"👤 User: {response['data']['name']}")
            print(f"📧 Email: {response['data']['email_id']}")
            print(f"📱 PAN: {response['data']['PAN']}")
            return True
        else:
            print(f"❌ Token validation failed!")
            print(f"Response: {response}")
            return False
            
    except Exception as e:
        print(f"❌ Error validating token: {e}")
        return False


# ============================================================
# TOKEN STATUS
# ============================================================

def show_token_status():
    """Display current token status"""
    print("\n" + "=" * 60)
    print("FYERS TOKEN STATUS")
    print("=" * 60)
    
    metadata = load_token_metadata()
    
    if not metadata:
        print("\n❌ No token metadata found")
        print("   Run initial token generation")
        return
    
    # Access Token
    print("\n📄 ACCESS TOKEN:")
    if 'access_token_generated' in metadata:
        gen_time = datetime.fromisoformat(metadata['access_token_generated'])
        exp_time = datetime.fromisoformat(metadata['access_token_expires'])
        remaining = exp_time - datetime.now()
        
        print(f"   Generated: {gen_time.strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"   Expires:   {exp_time.strftime('%Y-%m-%d %H:%M:%S')}")
        
        if remaining.total_seconds() > 0:
            hours = remaining.total_seconds() / 3600
            print(f"   Status:    ✅ Valid ({hours:.1f} hours remaining)")
        else:
            print(f"   Status:    ❌ Expired")
    
    # Refresh Token
    print("\n🔄 REFRESH TOKEN:")
    if 'refresh_token_generated' in metadata:
        gen_time = datetime.fromisoformat(metadata['refresh_token_generated'])
        exp_time = datetime.fromisoformat(metadata['refresh_token_expires'])
        remaining = exp_time - datetime.now()
        
        print(f"   Generated: {gen_time.strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"   Expires:   {exp_time.strftime('%Y-%m-%d %H:%M:%S')}")
        
        if remaining.total_seconds() > 0:
            days = remaining.total_seconds() / 86400
            print(f"   Status:    ✅ Valid ({days:.1f} days remaining)")
        else:
            print(f"   Status:    ❌ Expired")
    else:
        print("   Status:    ❌ Not available")
    
    # PIN
    print("\n🔐 PIN:")
    if load_pin():
        print("   Status:    ✅ Saved")
    else:
        print("   Status:    ❌ Not saved")
    
    print()


# ============================================================
# MAIN MENU
# ============================================================

def main():
    """Interactive menu"""
    while True:
        print("\n" + "=" * 60)
        print("FYERS API HELPER - WITH AUTO-REFRESH")
        print("=" * 60)
        print("1. Generate Access Token (Initial Setup - 15 day refresh)")
        print("2. Refresh Access Token (Using refresh token)")
        print("3. Get Valid Token (Auto-refresh if needed)")
        print("4. Validate Current Token")
        print("5. Show Token Status")
        print("6. Exit")
        print()
        
        choice = input("Select option (1-6): ").strip()
        
        if choice == "1":
            generate_access_token_initial()
        elif choice == "2":
            refresh_access_token()
        elif choice == "3":
            token = get_valid_access_token()
            if token:
                print(f"\n✅ Valid token: {token[:50]}...")
        elif choice == "4":
            validate_token_with_sdk()
        elif choice == "5":
            show_token_status()
        elif choice == "6":
            print("\n👋 Goodbye!")
            break
        else:
            print("❌ Invalid option")
        
        input("\nPress Enter to continue...")


if __name__ == "__main__":
    main()