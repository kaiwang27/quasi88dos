@echo off
Q88TEST.EXE > PORT.OUT
if errorlevel 1 goto fail
echo PASS > RESULT.TXT
goto end
:fail
echo FAIL > RESULT.TXT
:end
exit
