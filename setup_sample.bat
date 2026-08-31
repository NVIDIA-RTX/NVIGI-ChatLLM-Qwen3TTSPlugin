:: SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: MIT

@echo off
set cfg=vs2022
:loop
IF NOT "%1"=="" (
    IF "%1"=="vs2019" (
        SET cfg=vs2019
    ) ELSE IF "%1"=="vs2022" (
        SET cfg=vs2022
    ) ELSE (
        REM Unrecognized switch, print error message and exit
        echo Error: Unrecognized switch: %1
        exit /b 1
    )
    SHIFT
    GOTO :loop
)
echo Creating project files for %cfg%
call .\tools\premake5\premake5.exe %cfg% --file=.\sample.premake.lua
