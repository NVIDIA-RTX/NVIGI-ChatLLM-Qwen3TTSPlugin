:: SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: MIT

@echo off
setlocal

if exist .\_project\vs2022\sample.sln (
    rem find VS 2022
    for /f "usebackq tokens=1* delims=: " %%i in (`.\tools\vswhere.exe -version [17^,18^) -requires Microsoft.VisualStudio.Workload.NativeDesktop`) do (
        if /i "%%i"=="installationPath" (
            set VS_PATH=%%j
            GOTO :found_vs
        )
    )
    rem Try looking for VS Build Tools 2022
    for /f "usebackq tokens=1* delims=: " %%i in (`.\tools\vswhere.exe -products * -version [17^,18^) -requires Microsoft.VisualStudio.Workload.VCTools -requires Microsoft.VisualStudio.Workload.MSBuildTools`) do (
        if /i "%%i"=="installationPath" (
            set VS_PATH=%%j
            GOTO :found_vs
        )
    )
) else if exist .\_project\vs2019\sample.sln (
    rem find VS 2019
    for /f "usebackq tokens=1* delims=: " %%i in (`.\tools\vswhere.exe -version [16^,17^) -requires Microsoft.VisualStudio.Workload.NativeDesktop `) do (
        if /i "%%i"=="installationPath" (
            set VS_PATH=%%j
            GOTO :found_vs
        )
    )
    rem Try looking for VS Build Tools 2019
    for /f "usebackq tokens=1* delims=: " %%i in (`.\tools\vswhere.exe -products * -version [16^,17^) -requires Microsoft.VisualStudio.Workload.VCTools -requires Microsoft.VisualStudio.Workload.MSBuildTools`) do (
        if /i "%%i"=="installationPath" (
            set VS_PATH=%%j
            GOTO :found_vs
        )
    )
) else (
    rem find VS 2017
    for /f "usebackq tokens=1* delims=: " %%i in (`.\tools\vswhere.exe -version [15^,16^) -requires Microsoft.VisualStudio.Workload.NativeDesktop`) do (
        if /i "%%i"=="installationPath" (
            set VS_PATH=%%j
            GOTO :found_vs
        )
    )
)
:found_vs

echo off
set cfg=Debug
set bld=Clean,Build

:loop
IF NOT "%1"=="" (
    IF "%1"=="-fast" (
        set bld=Build
    )
    IF "%1"=="-Debug" (
        SET cfg=Debug
    )
    IF "%1"=="-debug" (
        SET cfg=Debug
    )
    IF "%1"=="-Release" (
        SET cfg=Release
    )
    IF "%1"=="-release" (
        SET cfg=Release
    )
    IF "%1"=="-Production" (
        SET cfg=Production
    )    
    IF "%1"=="-production" (
        SET cfg=Production
    )    
    SHIFT
    GOTO :loop
)

if not exist "%VS_PATH%" (
    echo "%VS_PATH%" not found. Is Visual Studio installed? && goto :ErrorExit
)

for /f "delims=" %%F in ('dir /b /s "%VS_PATH%\vsdevcmd.bat" 2^>nul') do set VSDEVCMD_PATH=%%F
echo ********Executing %VSDEVCMD_PATH%********
call "%VSDEVCMD_PATH%"
goto :SetVSEnvFinished

:ErrorExit
exit /b 1

:SetVSEnvFinished

if exist .\_project\vs2022\sample.sln (
    msbuild .\_project\vs2022\sample.sln /m /t:%bld% /property:Configuration=%cfg%
) else if exist .\_project\vs2019\sample.sln (
    msbuild .\_project\vs2019\sample.sln /m /t:%bld% /property:Configuration=%cfg%
) else (
    msbuild .\_project\vs2017\sample.sln /m /t:%bld% /property:Configuration=%cfg%
)

rem Project name must match the one declared in source/samples/nvigi.tts/qwen3tts/premake.lua
set sample_name=nvigi.basic.tts.cxx

if not exist .\_artifacts\%sample_name%\x64\%cfg%\%sample_name%.exe (
	goto :ErrorExit
)

set exePath=%cd%\_artifacts\%sample_name%\x64\%cfg%\%sample_name%.exe
set modelPath=%cd%\data\nvigi.models
set sdkPath=%cd%\_sdk\bin\x64
echo "%exePath%" --models "%modelPath%" --sdk "%sdkPath%" --text "Hello from Qwen3 TTS." > run_sample.bat
