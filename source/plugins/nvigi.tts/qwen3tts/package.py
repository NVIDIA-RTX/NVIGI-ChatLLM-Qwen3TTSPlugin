# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
#

# Component packaging configuration for Qwen3-TTS plugin
# This file is imported by tools/packaging/package.py

def get_components(externals):
    """
    Returns component definitions for this plugin.
    Args:
        externals: dict of external package definitions from main package.py
    """
    win_plat = ['win-x64']

    # ChatLLM.cpp (libchatllm.dll) imports cublas64_13.dll and cublasLt64_13.dll, both of which
    # ship alongside it in the chatllmcpp package. The CUDA 12 toolkit (cuda_ext / cublas64_12)
    # is not required; nvcuda / hybrid cudart come from the driver.
    chatllm_ext = [
        {"dep":"chatllm", "target":"win-x64", "path":"external/chatllmcpp",
         "name":"chatllmcpp", "version":"282c3c7a0877b8af97a7fde92526f880b6174160", "host_platform":"windows-x86_64"},
    ]

    chatllm_dlls = {'win-x64':[
        'external/chatllmcpp/libchatllm.dll',
        'external/chatllmcpp/cublas64_13.dll',
        'external/chatllmcpp/cublasLt64_13.dll',
        ],
    }

    components = {
        'tts.qwen3tts': {
            'platforms': win_plat,
            '3rdparty': chatllm_dlls,
            'sharedlib': ['nvigi.plugin.tts.qwen3tts'],
            'includes': ['source/plugins/nvigi.tts/nvigi_tts.h'],
            'models': ['nvigi.plugin.tts.qwen3tts'],
            'public_models': ['{E148704E-19D1-4FEA-B4F8-A6460053B296}'],
            'sources': ['plugins/nvigi.tts/nvigi_tts.h', 'plugins/nvigi.tts/qwen3tts'],
            'externals': chatllm_ext,
            'premake': 'source/plugins/nvigi.tts/qwen3tts/premake.lua',
            'data': ['nvigi.tts']
        }
    }

    return components
