@echo off
cd /d %~dp0
python scripts\png_to_assets_dedup.py 0.98 8
pause
