@ECHO OFF
REM Run this from the directory containing QUASI88.EXE and the ROM folder.
REM Keyboard and mouse input are recorded in KEYS.LOG and MOUSE.LOG.
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosvga -doskeylog -dosmouselog -dosframes 0
IF ERRORLEVEL 1 GOTO FAILED
ECHO QUASI88 exited normally.
GOTO DONE
:FAILED
ECHO QUASI88 reported an error.
GOTO DONE
:DONE
PAUSE
