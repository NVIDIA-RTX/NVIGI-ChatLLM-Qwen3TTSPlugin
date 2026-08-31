# Qwen3-TTS NVIDIA In-Game Inference AI Plugin

This repository is one example of an NVIDIA In-Game Inference (NVIGI) plugin built on
[ChatLLM.cpp](https://github.com/foldl/chatllm.cpp). ChatLLM.cpp is a C++ inference runtime
for GGML models, and the same library can back other NVIGI plugins, such as chat or
another speech model, by loading a different model and exposing a different NVIGI interface.

Qwen3-TTS is the speech example in this tree. It wraps the Qwen3-TTS family and supports
standard synthesis, speaker selection, three-second voice cloning from a reference clip,
and natural-language voice design.

GGML is statically linked into `libchatllm.dll`, so no GGML runtime DLLs are required. ChatLLM.cpp
manages its own GPU acceleration, which is why this plugin ships as a single backend rather than
separate CUDA/D3D12/Vulkan variants.

## Prerequisites

### Hardware

- NVIDIA RTX GPU. At q8_0, the 1.7B CustomVoice model uses about 4.3 GB of GPU memory and the
  0.6B CustomVoice model about 3.2 GB. See the performance section of
  `source/plugins/nvigi.tts/qwen3tts/README.md` for how those figures were measured.

### Windows

- Win10 20H1 (version 2004 - 10.0.19041) or newer
- Install graphics driver r555.85 or newer
- Install VS Code or VS2019/VS2022 with [SDK 10.0.19041+](https://go.microsoft.com/fwlink/?linkid=2120843)

## Setting up the plugin

Build steps for Powershell:
- Enter the root directory of the plugin
- Execute the following commands (replace -Release with -Debug or -Production if necessary)
```
.\setup.bat
.\build.bat -Release
.\package.bat -Release
```
By doing so you have created a directory _sdk where plugin files were copied.

# Downloading the data

Models are fetched from the public speech-model archive:
```
.\download_data.bat
```
Models land under `data\nvigi.models`.
Runtime low-mem selection (`--low-mem` on the sample) still chooses the 0.6B weights from that
directory; the download itself always pulls the full packaged set.

# Building and running a sample

The plugin comes with a simple sample that shows the possible integration of the plugin in software.
It also helps to confirm the plugin is correctly built. To build the sample follow these steps (using
Powershell)

- Enter the root directory of the plugin
- Execute the following commands
```
.\setup_sample.bat
.\build_sample.bat
```
To run the sample enter the following
```
.\run_sample.bat
```

See `source/plugins/nvigi.tts/qwen3tts/README.md` for the synthesis modes and the command line
options that drive them. Those modes apply in this repository too. The build and run steps in
that file apply when the plugin is built inside the NVIGI SDK; use the steps above here.

## DIRECTORY SETUP

Here you can find a short description of the directory setup. The directory setup mirrors that of the
NVIGI repository.

- `_artifacts` (appears after building; contains object files)
- `_project` (appears after setup; contains VS solution files)
- `_sdk` (appears after packaging; contains a plugin pack ready to be used)
- `data` (appears after data download; contains model files)
- `external` (appears after setup; contains third-party dependencies, including ChatLLM.cpp)
- `nvigi_core` (The NVIGI core framework; headers, libraries, DLLs)
- `premake` (contains scripts for managing the project)
- `source/`
  - `plugins` (contains the plugin source code)
  - `samples` (contains the sample source code)
  - `shared` (contains the shared headers)
- `tools` (contains tools for managing the project)

# Licensing

This project is licensed under the MIT License (`MIT-LICENSE.txt`).
Third-party licenses for Packman, ChatLLM.cpp, and ggml are in
`THIRD-PARTY-NOTICES.txt`.
`NOTICE.txt` covers downloaded model content, not third-party source licenses.

# NVIDIA In-Game Inference AI Plugins

Qwen3-TTS is a single plugin in that set. For NVIGI itself, and the official downloads, see
[NVIDIA In-Game Inferencing](https://developer.nvidia.com/rtx/in-game-inferencing)
