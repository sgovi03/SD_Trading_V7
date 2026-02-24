#!/usr/bin/env python3
"""
Fyers API Client - Clean Architecture
Handles all Fyers API interactions
"""

import json
import hashlib
import requests
from typing import Dict, List, Optional, Tuple
from datetime import datetime, timedelta
import logging

logger = logging.getLogger(__name__)


class FyersAPIClient:
    """Clean, configurable Fyers API client"""
    
    def __init__(self, config: Dict):
        """
        Initialize Fyers API client
        
        Args:
            config: Configuration dictionary from fyers_config.json
        """
        self.fyers_config = config['fyers']
        self.retry_config = config.get('retry', {})
        
        self.client_id = self.fyers_config['client_id']
        self.secret_key = self.fyers_config['secret_key']
        self.redirect_uri = self.fyers_config['redirect_uri']
        
        # Select environment
        env = self.fyers_config['environment']
        endpoints = self.fyers_config['endpoints'][env]
        
        self.api_base = endpoints['api_base']
        self.ws_base = endpoints['ws_base']
        
        self.access_token: Optional[str] = None
        self.refresh_token: Optional[str] = None
        
        logger.info(f"FyersAPIClient initialized for environment: {env}")
        logger.info(f"API Base: {self.api_base}")
    
    def generate_app_id_hash(self) -> str:
        """
        Generate appIdHash = SHA256(client_id:secret_key)
        
        Returns:
            Hex string of SHA-256 hash
        """
        hash_input = f"{self.client_id}:{self.secret_key}"
        hash_bytes = hashlib.sha256(hash_input.encode('utf-8')).digest()
        return hash_bytes.hex()
    
    def get_auth_url(self) -> str:
        """
        Generate authorization URL for OAuth flow
        
        Returns:
            Full authorization URL
        """
        params = {
            'client_id': self.client_id,
            'redirect_uri': self.redirect_uri,
            'response_type': 'code',
            'state': 'fyers_oauth_state'
        }
        
        param_str = '&'.join([f"{k}={v}" for k, v in params.items()])
        url = f"{self.api_base}/api/v3/generate-authcode?{param_str}"
        
        logger.info(f"Generated auth URL: {url}")
        return url
    
    def exchange_auth_code(self, auth_code: str) -> Tuple[bool, Optional[Dict]]:
        """
        Exchange authorization code for access token
        
        Args:
            auth_code: Authorization code from OAuth callback
            
        Returns:
            (success: bool, tokens: dict or None)
        """
        app_id_hash = self.generate_app_id_hash()
        
        url = f"{self.api_base}/api/v3/validate-authcode"
        payload = {
            "grant_type": "authorization_code",
            "appIdHash": app_id_hash,
            "code": auth_code
        }
        
        logger.info(f"Exchanging auth_code for access_token")
        logger.debug(f"POST {url}")
        
        try:
            response = requests.post(url, json=payload, timeout=30)
            data = response.json()
            
            if response.status_code == 200 and data.get('s') == 'ok':
                self.access_token = data.get('access_token')
                self.refresh_token = data.get('refresh_token')
                
                logger.info("✅ Token exchange successful")
                return True, data
            else:
                logger.error(f"Token exchange failed: {data.get('message', 'Unknown error')}")
                return False, data
                
        except Exception as e:
            logger.error(f"Token exchange error: {e}")
            return False, None
    
    def set_access_token(self, access_token: str):
        """Set access token manually"""
        self.access_token = access_token
        logger.info("Access token set manually")
    
    def _make_request(self, method: str, endpoint: str, **kwargs) -> Tuple[bool, Optional[Dict]]:
        """
        Make authenticated API request with retry logic
        
        Args:
            method: HTTP method (GET, POST, etc.)
            endpoint: API endpoint path
            **kwargs: Additional arguments for requests
            
        Returns:
            (success: bool, data: dict or None)
        """
        if not self.access_token:
            logger.error("No access token available")
            return False, {"error": "No access token"}
        
        # Add authentication header
        headers = kwargs.get('headers', {})
        headers['Authorization'] = f"{self.client_id}:{self.access_token}"
        kwargs['headers'] = headers
        
        # Retry logic
        max_attempts = self.retry_config.get('max_attempts', 3)
        backoff = self.retry_config.get('backoff_seconds', 5)
        
        for attempt in range(max_attempts):
            try:
                url = f"{self.api_base}{endpoint}"
                
                if method.upper() == 'GET':
                    response = requests.get(url, **kwargs)
                elif method.upper() == 'POST':
                    response = requests.post(url, **kwargs)
                else:
                    logger.error(f"Unsupported HTTP method: {method}")
                    return False, None
                
                # Check for valid JSON response
                try:
                    data = response.json()
                except json.JSONDecodeError:
                    logger.error(f"Invalid JSON response: {response.text[:200]}")
                    return False, {"error": "Invalid JSON", "response": response.text}
                
                if response.status_code == 200:
                    return True, data
                else:
                    logger.warning(f"API returned {response.status_code}: {data}")
                    
                    # Don't retry on authentication errors
                    if response.status_code in [401, 403]:
                        return False, data
                    
                    # Retry on other errors
                    if attempt < max_attempts - 1:
                        logger.info(f"Retrying in {backoff}s... (attempt {attempt + 1}/{max_attempts})")
                        import time
                        time.sleep(backoff)
                        if self.retry_config.get('exponential', True):
                            backoff *= 2
                    else:
                        return False, data
                        
            except requests.exceptions.RequestException as e:
                logger.error(f"Request error: {e}")
                if attempt < max_attempts - 1:
                    import time
                    time.sleep(backoff)
                else:
                    return False, {"error": str(e)}
        
        return False, None
    
    def validate_token(self) -> bool:
        """
        Validate current access token
        
        Returns:
            True if token is valid
        """
        success, data = self._make_request('GET', '/api/v3/profile', timeout=10)
        
        if success and data.get('s') == 'ok':
            logger.info("✅ Token is valid")
            if 'data' in data:
                logger.info(f"User: {data['data'].get('name', 'Unknown')}")
            return True
        else:
            logger.error("❌ Token validation failed")
            return False
    
    def get_historical_data(
        self,
        symbol: str,
        resolution: str,
        from_date: str,
        to_date: str
    ) -> Tuple[bool, Optional[List]]:
        """
        Fetch historical candle data
        
        Args:
            symbol: Trading symbol (e.g., "NSE:NIFTY25JANFUT")
            resolution: Candle resolution (e.g., "5" for 5 minutes)
            from_date: Start date (YYYY-MM-DD)
            to_date: End date (YYYY-MM-DD)
            
        Returns:
            (success: bool, candles: list or None)
        """
        params = {
            'symbol': symbol,
            'resolution': resolution,
            'date_format': '1',
            'range_from': from_date,
            'range_to': to_date,
            'cont_flag': ''
        }
        
        logger.info(f"Fetching historical data: {symbol} from {from_date} to {to_date}")
        
        success, data = self._make_request('GET', '/data/history', params=params, timeout=30)
        
        if success and data.get('s') == 'ok':
            candles = data.get('candles', [])
            logger.info(f"✅ Retrieved {len(candles)} candles")
            return True, candles
        else:
            logger.error(f"Failed to fetch historical data: {data}")
            return False, None
    
    def get_quote(self, symbol: str) -> Tuple[bool, Optional[Dict]]:
        """
        Get current quote/tick data
        
        Args:
            symbol: Trading symbol
            
        Returns:
            (success: bool, quote: dict or None)
        """
        params = {'symbol': symbol}
        
        success, data = self._make_request('GET', '/data/quotes', params=params, timeout=10)
        
        if success and data.get('s') == 'ok':
            return True, data.get('d', [{}])[0]
        else:
            return False, None
    
    def get_market_status(self) -> Tuple[bool, Optional[Dict]]:
        """
        Get current market status
        
        Returns:
            (success: bool, status: dict or None)
        """
        success, data = self._make_request('GET', '/api/v3/market-status', timeout=10)
        
        if success:
            return True, data
        else:
            return False, None
