@echo off
set JLINK_VM_OPTIONS=-Xmx512m
set DIR=%~dp0\bin
"%DIR%\javaw" %JLINK_VM_OPTIONS% -m VickyWarAnalyzer/ee.tkasekamp.vickywaranalyzer.gui.GUI %* | taskkill /F /IM cmd.exe
