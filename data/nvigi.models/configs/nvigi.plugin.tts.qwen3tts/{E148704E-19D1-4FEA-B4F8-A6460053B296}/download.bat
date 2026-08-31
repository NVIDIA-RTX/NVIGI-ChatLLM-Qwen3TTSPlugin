:: SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: MIT

@echo off
setlocal

REM Qwen3-TTS q8_0 weight set from the public speech-model archive.
set url=https://developer.nvidia.com/downloads/assets/ace/speech-model/nvigi.plugin.tts.qwen3tts_qwen3tts_12hz_q8_0_v1.1.zip

set models=Qwen3-TTS-12Hz-0.6B-Base-q8_0.bin Qwen3-TTS-12Hz-0.6B-CustomVoice-q8_0.bin Qwen3-TTS-12Hz-1.7B-Base-q8_0.bin Qwen3-TTS-12Hz-1.7B-CustomVoice-q8_0.bin Qwen3-TTS-12Hz-1.7B-VoiceDesign-q8_0.bin
set cards=model-cards/Qwen3-TTS-12Hz-0.6B-Base/README.md model-cards/Qwen3-TTS-12Hz-0.6B-CustomVoice/README.md model-cards/Qwen3-TTS-12Hz-1.7B-Base/README.md model-cards/Qwen3-TTS-12Hz-1.7B-CustomVoice/README.md model-cards/Qwen3-TTS-12Hz-1.7B-VoiceDesign/README.md model-cards/Qwen3-TTS-Tokenizer-12Hz/README.md
set payload=%models% %cards%

REM Two layouts to support. In the SDK repo this script lives under nvigi.models/configs/,
REM which holds model cards only; ai.h scans that path with optional=true but *requires*
REM nvigi.models/<plugin>/<guid> to exist, so weights must land there instead of next to
REM this script. In the shipped Developer Pack there is no configs/ level and the script
REM already sits in the model directory, so extracting in place is correct.
set "scriptdir=%~dp0"
set "scriptdir=%scriptdir:~0,-1%"
for %%i in ("%scriptdir%") do set "guiddir=%%~nxi"& set "lvl1=%%~dpi"
set "lvl1=%lvl1:~0,-1%"
for %%i in ("%lvl1%") do set "plugindir=%%~nxi"& set "lvl2=%%~dpi"
set "lvl2=%lvl2:~0,-1%"
for %%i in ("%lvl2%") do set "lvl2name=%%~nxi"& set "modelsroot=%%~dpi"

if /i "%lvl2name%"=="configs" (
    set "target=%modelsroot%%plugindir%\%guiddir%"
) else (
    set "target=%scriptdir%"
)

if not exist "%target%" mkdir "%target%"
echo Target: %target%

set missing=0
for %%f in (%models%) do if not exist "%target%\%%f" set missing=1
if "%missing%"=="0" (
    echo [skip] Qwen3-TTS weights already present.
    goto :done
)

echo Downloading Qwen3-TTS weights (about 8.7 GB)
curl -L --fail --retry 3 -C - -o "%target%\out.zip" "%url%"
if errorlevel 1 goto :failed

REM The package stores the .bin files flat at its root, which is the layout the plugin
REM expects: it derives the model directory from the first .bin and assumes the rest
REM are siblings. nvigi.model.config.json stays in the repo under configs/ and is not
REM in this archive. Original Hugging Face model cards are under model-cards/<repo>/README.md.
for %%f in (%payload%) do (
    echo [extract] %%f
    tar -x -f "%target%\out.zip" -C "%target%" %%f
    if errorlevel 1 goto :failed
)
del "%target%\out.zip"

:done
IF NOT "%1"=="-nopause" pause
exit /b 0

:failed
echo.
echo ERROR: Qwen3-TTS model download failed.
echo   %url%
echo   A partial out.zip is kept in %target%, so re-running resumes rather than restarting.
IF NOT "%1"=="-nopause" pause
exit /b 1
