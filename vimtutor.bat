:: Start Vim on a copy of the tutor file.
@echo off

:: Usage: vimtutor [-console] [xx]
::
:: -console means gvim will not be used
:: xx is a language code like "es" or "nl".
:: When an xx argument is given, it tries loading that tutor.
:: When this fails or no xx argument was given, it tries using 'v:lang'
:: When that also fails, it uses the English version.

:: Use Vim to copy the tutor, it knows the value of $VIMRUNTIME
FOR %%d in (. %TMP% %TEMP%) DO IF EXIST %%d\nul SET TUTORCOPY=%%d\$tutor$

SET xx=%1

IF NOT .%1==.-console GOTO use_gui
SHIFT
SET xx=%1
GOTO use_vim
:use_gui

:: Try making a copy of tutor with gvim.  If gvim cannot be found, try using
:: vim instead.  If vim cannot be found, alert user to check environment and
:: installation.

:: The script tutor.vim tells Vim which file to copy.
:: For Windows NT "start" works a bit differently.
IF .%OS%==.Windows_NT GOTO ntaction

start /w gvim -u NONE -c "so $VIMRUNTIME/tutor/tutor.vim"
IF ERRORLEVEL 1 GOTO use_vim

:: Start gvim without any .vimrc, set 'nocompatible'
start /w gvim -u NONE -c "set nocp" %TUTORCOPY%

GOTO end

:ntaction
start "dummy" /b /w gvim -u NONE -c "so $VIMRUNTIME/tutor/tutor.vim"
IF ERRORLEVEL 1 GOTO use_vim

:: Start gvim without any .vimrc, set 'nocompatible'
start "dummy" /b /w gvim -u NONE -c "set nocp" %TUTORCOPY%

GOTO end

:use_vim
:: The script tutor.vim tells Vim which file to copy
call vim -u NONE -c "so $VIMRUNTIME/tutor/tutor.vim"
IF ERRORLEVEL 1 GOTO no_executable

:: Start vim without any .vimrc, set 'nocompatible'
call vim -u NONE -c "set nocp" %TUTORCOPY%

GOTO end

:no_executable
ECHO.
ECHO.
ECHO No vim or gvim found in current directory or PATH.
ECHO Check your installation or re-run install.exe

:end
:: remove the copy of the tutor
IF EXIST %TUTORCOPY% DEL %TUTORCOPY%
SET xx=
