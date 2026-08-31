# Qwen3-TTS NVIGI Plugin

These instructions apply when this directory is inside the NVIGI SDK. In the standalone plugin repository, follow that repository's root README.

This directory contains an NVIGI plugin that uses ChatLLM.cpp as a DLL to perform text-to-speech synthesis with Qwen3-TTS models.

## Files

- `qwen3tts.cpp` - Main plugin implementation using ChatLLM.cpp
- `package.py` - required by setup.bat to add this project in the build
- `premake.lua` - build config
- `plugin.rc`, `resource.h`, `versions.h` - plugin boilerplate
- `README.md` - This file

This plugin uses a custom build of the ChatLLM.cpp project (custom version is hosted here: https://gitlab-master.nvidia.com/habilis/third-party/chatllm.cpp) that supports Qwen3-TTS inference in C++ via custom extensions to the libchatllm.h library bindings interface. As additional TTS functionality is implemented in ChatLLM (voice clone, flash attention, etc.) this plugin can be extended to expose that support.

## Building the Plugin

The Qwen3-TTS plugin is built as part of the normal SDK build. From the SDK root:

```cmd
setup.bat
```

Open the Visual Studio solution generated in `_project` and build the configuration you want (`Release`, `Debug`, or `Production`). That builds `nvigi.plugin.tts.qwen3tts` and the `nvigi.basic.tts.cxx` sample.

## Usage

Build output lands in `_artifacts`. Copy it into `bin\x64` with the configuration you just built:

```cmd
copy_sdk_binaries.bat Release
```

### Basic usage

The `nvigi.basic.tts.cxx` sample defaults to the `qwen3tts` backend:

```cmd
bin\x64\nvigi.basic.tts.cxx.exe --models data/nvigi.models --sdk bin\x64 --text "Well hi!"
```

This will generate an `output.wav` audio file.

### Synthesis modes

The model is selected automatically from the arguments you pass. Three modes are available:

| Mode | Trigger | Model loaded |
|-|-|-|
| CustomVoice (default) | none of the below | `Qwen3-TTS-12Hz-<size>-CustomVoice-q8_0.bin` |
| Voice clone | `--ref-audio` | `Qwen3-TTS-12Hz-<size>-Base-q8_0.bin` |
| VoiceDesign | `--instruct` (without `--ref-audio`) | `Qwen3-TTS-12Hz-1.7B-VoiceDesign-q8_0.bin` |

`--ref-audio` takes precedence over `--instruct`. Add `--low-mem` to any mode to load the 0.6B model instead of 1.7B (lower VRAM, faster). VoiceDesign is only available for 1.7B, so `--low-mem` is ignored there.

#### CustomVoice - pick a built-in speaker

```cmd
bin\x64\nvigi.basic.tts.cxx.exe --models data/nvigi.models --sdk bin\x64 --text "Well hi!" --speaker ryan
```

Available speakers: `vivian` (default), `serena`, `uncle_fu`, `ryan`, `aiden`, `ono_anna`, `sohee`, `eric`, `dylan`.

#### Voice clone - copy a voice from reference audio

Three seconds of reference audio is enough. Any format miniaudio can decode (WAV, MP3, FLAC, OGG); it is resampled to 24 kHz automatically.

```cmd
bin\x64\nvigi.basic.tts.cxx.exe --models data/nvigi.models --sdk bin\x64 --text "Well hi!" ^
    --ref-audio "C:\path\to\reference.mp3" --clone-mode 0
```

`--clone-mode 0` (XVEC, default) uses only the x-vector speaker embedding. `--clone-mode 1` (ICL) uses in-context learning and additionally requires `--ref-text` with the transcript of the reference audio:

```cmd
bin\x64\nvigi.basic.tts.cxx.exe --models data/nvigi.models --sdk bin\x64 --text "Well hi!" ^
    --ref-audio "C:\path\to\reference.wav" --clone-mode 1 --ref-text "transcript of the reference audio"
```

#### VoiceDesign - describe the voice in words

```cmd
bin\x64\nvigi.basic.tts.cxx.exe --models data/nvigi.models --sdk bin\x64 --text "Well hi!" ^
    --instruct "Male, 30s, warm and professional tone"
```

#### Lower memory

```cmd
bin\x64\nvigi.basic.tts.cxx.exe --models data/nvigi.models --sdk bin\x64 --text "Well hi!" --low-mem
```

Add `--play` to any of the above to play the audio through DirectSound as well as writing the WAV file.

## How It Works

The NVIGI plugin uses the C API from `libchatllm.dll` to:

1. Initialize the chatllm library
2. Load the specified Qwen3-TTS model
3. Offload every layer to the GPU (`-ngl 99`) and cap context at 4096 tokens
4. Process text input through the TTS model
5. Capture generated audio data

Additionally, the sample will write the audio output as a WAV file.

## Performance

Real-time factor is seconds of compute per second of audio. Lower is faster. Collected with `nvigi.basic.tts.cxx.exe --bench 4` on a GeForce RTX 5090: one warm-up iteration, then the mean of the next three. The Release plugin offloads every layer (`-ngl 99`) and caps context at 4096 tokens. Both rows are CustomVoice.

Short prompt (50 characters): "Well hi, this is a short Qwen3-TTS benchmark line."

Long prompt (159 characters): "Well hi, this is the longer Qwen3-TTS benchmark line, used to measure how real-time factor changes when the spoken passage is several sentences instead of one."

Max prompt (3700 characters): "The quick brown fox keeps speaking so this benchmark can measure a passage near the context limit. " repeated to that length. It produces about 3.6 minutes of audio. The 4096-token context cap is about 5 minutes of speech.

| Model | Short RTF | Long RTF | Max RTF | GPU memory |
|-|-|-|-|-|
| 0.6B CustomVoice | 1.38 | 1.23 | 1.23 | 3.2 GB |
| 1.7B CustomVoice | 1.35 | 1.33 | 1.40 | 4.3 GB |

GPU memory is the increase in `nvidia-smi` memory.used from an idle reading of about 2.1 GB while the sample is loaded. On Windows WDDM that figure is a GPU-wide delta.

## Troubleshooting

- Ensure `libchatllm.dll` and other required DLLs are in the same directory as the executable
- Verify the model file exists and is the quantized .bin format
- Make sure you have enough RAM/VRAM for the model
- Check console output for error messages

## Notes

- GPU acceleration requires a CUDA-enabled build of ChatLLM.cpp
- The plugin offloads every layer (`-ngl 99`) and caps context at 4096 tokens
