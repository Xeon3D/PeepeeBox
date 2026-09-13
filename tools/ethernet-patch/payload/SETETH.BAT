@echo off
rem -----------------------------------------------------------
rem SETETH ON   - fun.net data transmission over Ethernet
rem               (packet driver, see TRANSMIT.BAT / WATTCP.ETH)
rem SETETH OFF  - back to the modem (PPP over COM4)
rem SETETH      - show the current setting (errorlevel 1 = Ethernet)
rem -----------------------------------------------------------
if "%1"=="ON"  goto ON
if "%1"=="on"  goto ON
if "%1"=="OFF" goto OFF
if "%1"=="off" goto OFF

\FN_SYS\FN_SYS /conntype
if ERRORLEVEL 1 goto ISETH
echo CONNTYPE = MODEM
goto ENDE
:ISETH
echo CONNTYPE = ETHERNET
goto ENDE

:ON
\FN_SYS\FN_SYS /setting=CONNTYPE;ETHERNET
echo CONNTYPE = ETHERNET
goto ENDE

:OFF
\FN_SYS\FN_SYS /setting=CONNTYPE;MODEM
echo CONNTYPE = MODEM

:ENDE
