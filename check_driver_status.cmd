@echo off
echo ================================================
echo   PC Cleanup Driver Diagnostic Tool
echo ================================================
echo.

echo [1] Checking if service exists...
sc query PCCleanupDriver
echo.

echo [2] Checking if driver file exists...
if exist "C:\Users\tai2l\source\repos\Mira Loader\x64\Release\KMDFDriver7.sys" (
    echo [+] Driver file found
    dir "C:\Users\tai2l\source\repos\Mira Loader\x64\Release\KMDFDriver7.sys"
) else (
    echo [-] Driver file NOT found
)
echo.

echo [3] Checking symbolic link with dir command...
dir \\.\PCCleanupDriver 2>nul
if %errorlevel% equ 0 (
    echo [+] Symbolic link EXISTS at \\.\PCCleanupDriver
) else (
    echo [-] Symbolic link NOT FOUND at \\.\PCCleanupDriver
)
echo.

echo [4] Checking test signing status...
bcdedit /enum {current} | findstr /i testsigning
echo.

echo [5] Recent System Events (last 10 entries)...
wevtutil qe System /c:10 /rd:true /f:text /q:"*[System[Provider[@Name='Service Control Manager'] and TimeCreated[timediff(@SystemTime) <= 3600000]]]"
echo.

echo ================================================
echo Diagnostic Complete
echo ================================================
pause
