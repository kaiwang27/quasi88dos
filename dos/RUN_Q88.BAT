@ECHO OFF
REM Change to this folder first; the local executable and ROM directory are required.
REM Keyboard and mouse input are recorded in KEYS.LOG and MOUSE.LOG.
CD
IF NOT EXIST QUASI88.EXE GOTO NOEXE
IF NOT EXIST ROM\N88.ROM GOTO NOROM
QUASI88.EXE -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosvga -doskeylog -dosmouselog -dosframes 0
IF ERRORLEVEL 1 GOTO FAILED
ECHO QUASI88 exited normally.
GOTO DONE
:NOEXE
ECHO QUASI88.EXE is not in the current directory. Change to the test folder first.
GOTO DONE
:NOROM
ECHO ROM\N88.ROM is missing. Copy your ROMs into the ROM subdirectory first.
GOTO DONE
:FAILED
ECHO QUASI88 reported an error.
GOTO DONE
:DONE
PAUSE
