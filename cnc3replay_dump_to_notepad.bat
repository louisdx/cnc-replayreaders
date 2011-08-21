@echo off

cd "%~dp0"
%~d0

if not exist cnc3reader.exe goto noexe

cnc3reader -e %1 > cnc3reader_dump_tmp.txt

if not "%ERRORLEVEL%" == "0" goto error

start notepad cnc3reader_dump_tmp.txt

goto done

:error

echo.
echo.
echo There has been an error opening the replay file:
echo    %1
echo.
echo Press any key to abort the operation, and double-check that
echo you have a valid Tiberium Wars, Kane's Wrath or Red Alert 3 replay.

pause > NUL:

goto done

:noexe

echo The file "cnc3reader.exe" could not be found in the directory %~dp0.

pause

goto done

:done
