taskkill /F /IM run_v8_live.exe 2>nul
timeout /t 5 /nobreak >nul

.\build\bin\Release\run_v8_live.exe