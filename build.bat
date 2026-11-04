@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set INC=C:\Users\Owner\source\repos\Project2\vcpkg_installed\x64-windows\include
set LIB_DIR=C:\Users\Owner\source\repos\Project2\vcpkg_installed\x64-windows\lib
cd /d e:\Chatting

echo === Building Server ===
cl /nologo /utf-8 /EHsc /std:c++17 /MD /D_WIN32_WINNT=0x0601 /I "%INC%" Server.cpp /link /LIBPATH:"%LIB_DIR%" /OUT:Server.exe
echo Server exit: %ERRORLEVEL%

echo === Building Client ===
cl /nologo /utf-8 /EHsc /std:c++17 /MD /D_WIN32_WINNT=0x0601 /I "%INC%" Client.cpp /link /LIBPATH:"%LIB_DIR%" /OUT:Client.exe
echo Client exit: %ERRORLEVEL%
