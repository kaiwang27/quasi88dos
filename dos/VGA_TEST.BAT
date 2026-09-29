@ECHO OFF
REM Run this from the directory containing QUASI88.EXE and the ROM folder.
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosvga -dosvideochk -dosframes 3
IF ERRORLEVEL 1 GOTO FAILED
ECHO VGA test passed.
GOTO DONE
:FAILED
ECHO VGA test failed. Check the ROM files and messages above.
:DONE
PAUSE
