@echo off

cd "%~dp0"
%~d0

if not exist cnc4reader.exe goto noexe

cnc4reader %1 > cnc4reader_dump_tmp.txt

if not "%ERRORLEVEL%" == "0" goto error

start notepad cnc4reader_dump_tmp.txt

goto done

:error

echo.
echo.
echo There has been an error opening the replay file:
echo    %1
echo.
echo Press any key to abort the operation, and double-check
echo that you have a valid Tiberian Twilight replay.

pause > NUL:

goto done

:noexe

echo The file "cnc4reader.exe" could not be found in the directory %~dp0.

pause

goto done

:done
