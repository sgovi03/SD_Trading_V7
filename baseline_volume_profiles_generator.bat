cd D:\SD_System\SD_Volume_OI\Scanner\SD_Trading_V7

REM Generate baselines for ALL symbols in the DB
python scripts/generate_baselines_from_db.py

REM Generate for one symbol only
python scripts/generate_baselines_from_db.py --symbol NIFTY-FUT

REM See what symbols + bar counts are in the DB
python scripts/generate_baselines_from_db.py --list-symbols

REM Use 30-day lookback instead of default 60
python scripts/generate_baselines_from_db.py --lookback 30