@echo off
set JLINK_VM_OPTIONS=-Xmx512m
set DIR=%~dp0\bin
"%DIR%\javaw" %JLINK_VM_OPTIONS% -m vic2sgea/org.victoria2.tools.vic2sgea.gui.Main %* | taskkill /F /IM cmd.exe
