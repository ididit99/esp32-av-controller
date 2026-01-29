@echo off
set PIO=c:\Users\Josh\.platformio\penv\Scripts\pio.exe

echo ==========================================
echo 1. Build Firmware
echo 2. Build Filesystem (LittleFS)
echo 3. Upload Filesystem
echo 4. Upload Firmware
echo 5. Build & Upload All (FS + FW)
echo ==========================================
set /p opt="Select option: "

if "%opt%"=="1" %PIO% run
if "%opt%"=="2" %PIO% run -t buildfs
if "%opt%"=="3" %PIO% run -t uploadfs
if "%opt%"=="4" %PIO% run -t upload
if "%opt%"=="5" (
    %PIO% run -t buildfs
    %PIO% run -t uploadfs
    %PIO% run -t upload
)

pause
