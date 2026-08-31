:: SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: MIT

pushd %~dp0

REM set paths
set nvigi_core=nvigi_core\bin\x64
set chatllm=external\chatllmcpp
set plugin_name=nvigi.plugin.tts.qwen3tts

REM choose config
set cfg=Release
IF NOT "%1"=="" (
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
)

REM remove previous pack
if exist .\_sdk rmdir .\_sdk /s /q

REM create directory structure
mkdir .\_sdk
mkdir .\_sdk\bin
mkdir .\_sdk\bin\x64
mkdir .\_sdk\include
mkdir .\_sdk\symbols

REM fill in bin/x64
copy .\_artifacts\%plugin_name%\x64\%cfg%\%plugin_name%.dll .\_sdk\bin\x64\
copy .\%nvigi_core%\%cfg%\amd_ags_x64.dll .\_sdk\bin\x64\
copy .\%nvigi_core%\%cfg%\nvigi.core.framework.dll .\_sdk\bin\x64\
copy .\%nvigi_core%\%cfg%\nvigi.plugin.hwi.common.dll .\_sdk\bin\x64\
copy .\%nvigi_core%\%cfg%\nvigi.plugin.hwi.cuda.dll .\_sdk\bin\x64\
copy .\%nvigi_core%\%cfg%\nvigi.plugin.hwi.d3d12.dll .\_sdk\bin\x64\
REM ChatLLM.cpp: GGML is statically linked; cuBLAS 13 DLLs ship in the same chatllmcpp package.
REM nvcuda.dll and nvcudart_hybrid64.dll come from the driver.
copy .\%chatllm%\libchatllm.dll .\_sdk\bin\x64\
copy .\%chatllm%\cublas64_13.dll .\_sdk\bin\x64\
copy .\%chatllm%\cublasLt64_13.dll .\_sdk\bin\x64\

REM fill in symbols
copy .\_artifacts\%plugin_name%\x64\%cfg%\%plugin_name%.pdb .\_sdk\symbols\
copy .\nvigi_core\symbols\x64\%cfg%\nvigi.core.framework.pdb .\_sdk\symbols\

REM fill in includes
robocopy .\nvigi_core\source\core\nvigi.api\ .\_sdk\include\
copy .\nvigi_core\source\utils\nvigi.ai\nvigi_ai.h .\_sdk\include\
copy .\nvigi_core\source\utils\nvigi.ai\nvigi_stl_helpers.h .\_sdk\include\
copy .\source\plugins\nvigi.tts\nvigi_tts.h .\_sdk\include\
popd

REM fill remaining
copy .\MIT-LICENSE.txt .\_sdk\
copy .\README.md .\_sdk\
copy .\NOTICE.txt .\_sdk\
copy .\THIRD-PARTY-NOTICES.txt .\_sdk\
copy .\plugin.project.xml .\_sdk\
copy .\plugin.premake.lua .\_sdk\
