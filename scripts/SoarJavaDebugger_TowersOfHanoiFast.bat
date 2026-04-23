@echo off
set SOAR_HOME=%~dp0
set PATH=%SOAR_HOME%;%PATH%

if "%SOAR_DEBUGGER_SOURCE_FILE%"=="" (
    set SOURCE_FILE=%SOAR_HOME%..\UnitTests\SoarTestAgents\FunctionalTests_testTowersOfHanoiFast.soar
) else (
    set SOURCE_FILE=%SOAR_DEBUGGER_SOURCE_FILE%
)

if exist "%SOURCE_FILE%" (
    start javaw -Djava.library.path="%SOAR_HOME%" -jar "%SOAR_HOME%"\SoarJavaDebugger.jar -agent towersOfHanoiFast -source "%SOURCE_FILE%" %1 %2 %3 %4 %5
) else (
    echo Warning: Towers of Hanoi Fast source file not found: "%SOURCE_FILE%"
    echo Set SOAR_DEBUGGER_SOURCE_FILE to override the startup source path.
    start javaw -Djava.library.path="%SOAR_HOME%" -jar "%SOAR_HOME%"\SoarJavaDebugger.jar %1 %2 %3 %4 %5
)