:: SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: MIT

@echo off
call "%~dp0data\nvigi.models\configs\nvigi.plugin.tts.qwen3tts\{E148704E-19D1-4FEA-B4F8-A6460053B296}\download.bat" -nopause
exit /b %ERRORLEVEL%
