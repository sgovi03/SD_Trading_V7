"""
SD Trading System - Configuration Loader
FIXED: Looks for config in parent directory first
"""

import json
import logging
from pathlib import Path
from typing import Dict, Any, Optional
from datetime import datetime


class SystemConfig:
    """
    Centralized configuration manager
    Reads system_config.json and provides easy access to all paths
    """
    
    def __init__(self, config_file: str = 'system_config.json'):
        """
        Initialize configuration
        
        Args:
            config_file: Path to system configuration file
        """
        # Try to find config file in multiple locations
        self.config_file = self._find_config_file(config_file)
        self.config: Dict[str, Any] = {}
        self.root_dir: Optional[Path] = None
        
        self._load_config()
        self._setup_root()
        self._create_directories()
    
    def _find_config_file(self, config_file: str) -> Path:
        """Find config file in multiple possible locations"""
        script_dir = Path(__file__).resolve().parent
        search_paths = [
            Path(config_file),              # Current directory
            Path('..') / config_file,       # Parent directory
            Path('../..') / config_file,    # Two levels up
            Path.cwd() / config_file,       # Absolute current
            Path.cwd().parent / config_file, # Absolute parent
            script_dir / config_file,       # Directory of this script
            script_dir.parent / config_file, # One level above script
            script_dir.parent.parent / config_file # Two levels above script
        ]
        
        for path in search_paths:
            if path.exists():
                print(f"[OK] Found config at: {path.absolute()}")
                return path
        
        # If not found, return the default and let it fail with clear error
        return Path(config_file)
        
    def _load_config(self):
        """Load configuration from JSON file"""
        if not self.config_file.exists():
            raise FileNotFoundError(
                f"Configuration file not found: {self.config_file.absolute()}\n"
                f"Current directory: {Path.cwd()}\n"
                f"Please ensure system_config.json exists in the root directory"
            )
        
        try:
            with open(self.config_file, 'r') as f:
                self.config = json.load(f)
        except json.JSONDecodeError as e:
            raise ValueError(f"Invalid JSON in config file: {e}")
    
    def _setup_root(self):
        """Setup root directory"""
        # Use config file's parent as root if not specified
        root = self.config.get('system', {}).get('root_directory')
        if root:
            self.root_dir = Path(root)
        else:
            # Use the directory containing the config file
            self.root_dir = self.config_file.parent.absolute()
            print(f"📁 Using root directory: {self.root_dir}")
    
    def _create_directories(self):
        """Create all directories specified in config"""
        for dir_name, dir_path in self.config.get('directories', {}).items():
            full_path = self.root_dir / dir_path
            full_path.mkdir(parents=True, exist_ok=True)
            
        # Also create parent directories for files
        for file_name, file_path in self.config.get('files', {}).items():
            full_path = self.root_dir / file_path
            full_path.parent.mkdir(parents=True, exist_ok=True)
    
    def get_path(self, key: str, create_parent: bool = True) -> Path:
        """
        Get file path from configuration
        
        Args:
            key: File key from config['files']
            create_parent: Create parent directory if it doesn't exist
            
        Returns:
            Full path to file
        """
        file_path = self.config.get('files', {}).get(key)
        if not file_path:
            raise KeyError(f"File path '{key}' not found in configuration")
        
        full_path = self.root_dir / file_path
        
        if create_parent:
            full_path.parent.mkdir(parents=True, exist_ok=True)
        
        return full_path
    
    def get_directory(self, key: str, create: bool = True) -> Path:
        """
        Get directory path from configuration
        
        Args:
            key: Directory key from config['directories']
            create: Create directory if it doesn't exist
            
        Returns:
            Full path to directory
        """
        dir_path = self.config.get('directories', {}).get(key)
        if not dir_path:
            raise KeyError(f"Directory '{key}' not found in configuration")
        
        full_path = self.root_dir / dir_path
        
        if create:
            full_path.mkdir(parents=True, exist_ok=True)
        
        return full_path
    
    def get_value(self, *keys: str, default: Any = None) -> Any:
        """
        Get nested configuration value
        
        Args:
            *keys: Nested keys to traverse (e.g., 'fyers', 'client_id')
            default: Default value if key not found
            
        Returns:
            Configuration value
        """
        value = self.config
        for key in keys:
            if isinstance(value, dict):
                value = value.get(key)
                if value is None:
                    return default
            else:
                return default
        return value
    
    def get_fyers_config(self) -> Dict[str, Any]:
        """Get Fyers-specific configuration"""
        return self.config.get('fyers', {})
    
    def get_trading_config(self) -> Dict[str, Any]:
        """Get trading configuration"""
        return self.config.get('trading', {})
    
    def get_bridge_config(self) -> Dict[str, Any]:
        """Get bridge configuration"""
        return self.config.get('bridge', {})
    
    def get_strategy_config_path(self) -> Path:
        """Get path to strategy configuration file"""
        return self.get_path('strategy_config')
    
    def get_log_file(self, component: str = 'python') -> Path:
        """
        Get log file path for component
        
        Args:
            component: 'python' or 'cpp'
            
        Returns:
            Path to log file
        """
        log_config = self.config.get('logging', {}).get(component, {})
        log_file = log_config.get('file')
        
        if not log_file:
            # Fallback
            if component == 'python':
                log_file = 'logs/fyers_bridge.log'
            else:
                log_file = 'logs/sd_engine.log'
        
        return self.root_dir / log_file
    
    def setup_logging(self, component: str = 'python') -> logging.Logger:
        """
        Setup logging for component
        
        Args:
            component: 'python' or 'cpp'
            
        Returns:
            Configured logger
        """
        log_config = self.config.get('logging', {}).get(component, {})
        log_file = self.get_log_file(component)
        log_level = log_config.get('level', 'INFO')
        
        # Create logger
        logger = logging.getLogger(component)
        logger.setLevel(getattr(logging, log_level))
        
        # Remove existing handlers
        logger.handlers = []
        
        # File handler
        log_file.parent.mkdir(parents=True, exist_ok=True)
        file_handler = logging.FileHandler(log_file, encoding='utf-8')
        file_handler.setLevel(getattr(logging, log_level))
        
        # Console handler
        console_handler = logging.StreamHandler()
        console_handler.setLevel(getattr(logging, log_level))
        
        # Formatter
        formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(formatter)
        console_handler.setFormatter(formatter)
        
        logger.addHandler(file_handler)
        logger.addHandler(console_handler)
        
        return logger
    
    def get_environment(self) -> str:
        """Get current environment (test/production)"""
        return self.config.get('system', {}).get('environment', 'test')
    
    def is_test_mode(self) -> bool:
        """Check if running in test mode"""
        return self.get_environment() == 'test'
    
    def is_production_mode(self) -> bool:
        """Check if running in production mode"""
        return self.get_environment() == 'production'
    
    def get_fyers_endpoint(self, endpoint_type: str = 'api_base') -> str:
        """
        Get Fyers API endpoint for current environment
        
        Args:
            endpoint_type: 'api_base' or 'ws_base'
            
        Returns:
            Endpoint URL
        """
        env = self.get_environment()
        endpoints = self.config.get('fyers', {}).get('endpoints', {})
        return endpoints.get(env, {}).get(endpoint_type, '')
    
    def validate(self) -> bool:
        """
        Validate configuration
        
        Returns:
            True if valid, raises exception otherwise
        """
        required_sections = ['system', 'directories', 'files', 'fyers', 'trading']
        
        for section in required_sections:
            if section not in self.config:
                raise ValueError(f"Missing required config section: {section}")
        
        # Validate Fyers config
        fyers = self.config.get('fyers', {})
        if not fyers.get('client_id'):
            raise ValueError("Missing fyers.client_id in configuration")
        
        # Validate directories
        dirs = self.config.get('directories', {})
        if not dirs:
            raise ValueError("No directories configured")
        
        # Validate files
        files = self.config.get('files', {})
        if not files:
            raise ValueError("No files configured")
        
        return True
    
    def summary(self) -> str:
        """Get configuration summary"""
        lines = [
            "=" * 60,
            "SD TRADING SYSTEM - CONFIGURATION",
            "=" * 60,
            f"Root Directory: {self.root_dir}",
            f"Environment: {self.get_environment()}",
            f"Config File: {self.config_file.absolute()}",
            "",
            "Directories:",
        ]
        
        for name, path in self.config.get('directories', {}).items():
            full_path = self.root_dir / path
            exists = "✓" if full_path.exists() else "✗"
            lines.append(f"  [{exists}] {name:20s} -> {full_path}")
        
        lines.append("")
        lines.append("Key Files:")
        for name, path in self.config.get('files', {}).items():
            full_path = self.root_dir / path
            exists = "✓" if full_path.exists() else "✗"
            lines.append(f"  [{exists}] {name:20s} -> {full_path}")
        
        lines.append("")
        lines.append("Fyers Configuration:")
        fyers = self.get_fyers_config()
        lines.append(f"  Client ID: {fyers.get('client_id')}")
        lines.append(f"  Environment: {fyers.get('environment')}")
        lines.append(f"  API Endpoint: {self.get_fyers_endpoint('api_base')}")
        
        lines.append("")
        lines.append("Trading Configuration:")
        trading = self.get_trading_config()
        lines.append(f"  Symbol: {trading.get('symbol')}")
        lines.append(f"  Mode: {trading.get('mode')}")
        lines.append(f"  Capital: ₹{trading.get('starting_capital')}")
        
        lines.append("=" * 60)
        
        return "\n".join(lines)


# Convenience function
def load_config(config_file: str = 'system_config.json') -> SystemConfig:
    """
    Load system configuration
    
    Args:
        config_file: Path to configuration file
        
    Returns:
        SystemConfig instance
    """
    config = SystemConfig(config_file)
    config.validate()
    return config


# Example usage
if __name__ == '__main__':
    try:
        config = load_config()
        print(config.summary())
        
        # Test path access
        print("\nTesting path access:")
        print(f"Live data CSV: {config.get_path('live_data_csv')}")
        print(f"Logs directory: {config.get_directory('logs')}")
        print(f"Token file: {config.get_path('fyers_token')}")
        
    except Exception as e:
        print(f"Error: {e}")