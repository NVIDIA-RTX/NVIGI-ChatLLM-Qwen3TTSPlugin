:: SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: MIT

@echo off
setlocal enabledelayedexpansion
set cfg=vs2022
set comps=""
:loop
IF NOT "%1"=="" (
    IF "%1"=="vs2019" (
        SET cfg=vs2019
    ) ELSE IF "%1"=="vs2022" (
        SET cfg=vs2022
    ) ELSE IF "%1"=="-components" (
        SET comps=-components %2
        SHIFT
    ) ELSE IF "%1"=="-c" (        
        SET comps=-components %2
        SHIFT
    ) ELSE (
        REM Unrecognized switch, print error message and exit
        echo Error: Unrecognized switch: %1
        exit /b 1
    )
    SHIFT
    GOTO :loop
)

IF "%cfg%"=="" (
    IF exist .\_project\vs2022\aiinferencemananger.sln (
        SET cfg=vs2022
    ) ELSE IF exist .\_project\vs2019\aiinferencemananger.sln (
        SET cfg=vs2019
    )
)

REM Pull the basic tools we need for the next steps
call .\tools\packman\packman.cmd pull -p windows-x86_64 .\tools\project.tools.xml

if exist plugin.project.xml call .\tools\packman\packman.cmd pull -p windows-x86_64 plugin.project.xml
echo Creating project files for %cfg%
call .\tools\premake5\premake5.exe %cfg% --file=.\plugin.premake.lua
