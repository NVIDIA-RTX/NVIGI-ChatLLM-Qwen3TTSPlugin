// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include <iostream>
#include <format>
#include <chrono>
#include <thread>
#include <vector>
#include <numeric>
#include <algorithm>

// Windows and D3D12 Agility SDK exports
#include <windows.h>
extern "C" __declspec(dllexport) UINT         D3D12SDKVersion = 615;
extern "C" __declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";

// NVIGI includes
#include <nvigi.h>
#include <nvigi_tts.h>
#include <nvigi_d3d12.h>
#include <nvigi_vulkan.h>
#include <nvigi_stl_helpers.h>

// C++ wrappers (from shared location)
#include "cxx_wrappers/clparser.hpp"
#include "cxx_wrappers/core.hpp"
#include "cxx_wrappers/d3d12.hpp"
#include "cxx_wrappers/vulkan.hpp"
#include "cxx_wrappers/tts/tts.hpp"

// Audio playback utilities
#ifdef NVIGI_WINDOWS
#include "nvigi_core/source/utils/nvigi.dsound/player.h"

// Simple audio player helper
struct AudioPlayer {
    static void play_audio(const int16_t* samples, size_t count) {
        // Must match kSampleRate in the shared TTS wrapper; see the note there.
        static nvigi::utils::Player player(16, 24000);
        nvigi::utils::Buffer buffer(player, samples, static_cast<DWORD>(count * sizeof(int16_t)));
        buffer.Play();
        buffer.Wait();
    }
};
#endif

using namespace nvigi::tts;

static bool g_logToConsole = false;

void loggingCallback(nvigi::LogType type, const char* msg)
{
    if (g_logToConsole)
    {
        printf("%s", msg);
    }
}

// Returns seconds of compute per second of audio produced
static float generate_speech(std::unique_ptr<Instance>& instance, nvigi::tts::RuntimeConfig config, std::string to_speak, std::string target_path) {

    size_t total_samples = 0;
    auto start_time = std::chrono::steady_clock::now();

    std::cout << "Generating";
    std::cout.flush();

    // Generate speech (blocking call)
    auto result = instance->generate(
        to_speak,
        target_path,
        config,
        [&total_samples](const int16_t* audio, size_t samples, ExecutionState state) -> ExecutionState {
            if (state == ExecutionState::DataPending || state == ExecutionState::Done) {
                total_samples += samples;
            }
            return state; // Continue normally
        }
    );

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    if (!result) {
        std::cerr << "\nError: " << result.error().what() << "\n";
        return -1.0f;
    }
    if (total_samples == 0) {
        std::cerr << "\nError: no audio produced\n";
        return -1.0f;
    }
    const float elapsed = duration.count() / 1000.0f;
    const float audio = total_samples / static_cast<float>(kSampleRate);
    std::cout << " done\n";
    std::cout << "Generation Time: " << elapsed << " seconds for " << audio << " seconds of audio\n";

    // Sampling is nondeterministic, so normalize time by audio produced.
    return elapsed / audio;
}

int main(int argc, char** argv) {
    try {
        // Parse command line arguments
        auto parser = clparser::create_parser();
        parser.add_command("", "sdk", " sdk location, if none provided assuming exe location", "", false)
            .add_command("", "plugin", " plugin location, if none provided assuming sdk location", "")
            .add_command("m", "models", " model repo location", "", false)
            .add_command("t", "threads", " number of threads", "8")
            .add_command("", "backend", " backend to use - d3d12, cuda, vulkan, template, qwen3tts", "qwen3tts")
            .add_command("", "guid", " TTS model guid in registry format", "{E148704E-19D1-4FEA-B4F8-A6460053B296}")
            .add_command("", "vram", " the amount of vram to use in MB", "8192")
            .add_command("", "log-level", " logging level 0-2", "2")
            .add_command("", "log-console", " print NVIGI log messages to the console")
            .add_command("", "text", " text to synthesize", "Well hi!")
            .add_command("", "target", " path to target voice spectrogram", "", false) // not required for Qwen3-TTS
            .add_command("", "output", " output WAV file path", "output.wav")
            .add_command("", "speed", " speech speed (0.5 - 2.0)", "1.0")
            .add_command("", "language", " language code (en, en-us, en-uk, es, de)", "en")
            .add_command("", "timesteps", " number of timesteps for TTS inference (16-32)", "16")
            .add_command("", "bench", " run N timed iterations and report real-time factor", "0")
            .add_command("", "async", " use async mode (polled, non-blocking)")
            .add_command("", "play", " play audio in real-time using DirectSound")
            .add_command("", "print-system-info", " print system information", "")
            .add_command("", "ref-audio", " reference audio file for voice cloning (qwen3tts)", "", false)
            .add_command("", "ref-text", " transcript of ref audio for ICL mode (qwen3tts)", "", false)
            .add_command("", "clone-mode", " voice clone mode: 0=XVEC, 1=ICL (qwen3tts)", "0", false)
            .add_command("", "speaker", " speaker name: vivian, serena, ryan, etc (qwen3tts)", "", false)
            .add_command("", "instruct", " voice description for VoiceDesign mode (qwen3tts)", "", false)
            .add_command("", "low-mem", " use 0.6B model instead of 1.7B (qwen3tts)");
        parser.parse(argc, argv);

        g_logToConsole = parser.has("log-console");

        {
            // Initialize NVIGI core
            nvigi::Core core({
                .sdkPath = parser.get("sdk"),
                .logLevel = static_cast<nvigi::LogLevel>(parser.get_int("log-level")),
                .showConsole = false,
                .pathToLogsAndData = parser.get("sdk"),
                .logMessageCallback = loggingCallback
                });

            // Print system info if requested
            if (parser.has("print-system-info")) {
                core.getSystemInfo().print();
                std::cout << std::flush;
            }

            // Default empty configs
            nvigi::vulkan::VulkanConfig vk_config{};
            nvigi::d3d12::D3D12Config d3d12_config{};
            nvigi::vulkan::VulkanObjects vk_objects{};
            nvigi::d3d12::DeviceAndQueue deviceAndQueue{};

            // Initialize configs as needed
            if (parser.get("backend") == "vulkan") {
                vk_objects = nvigi::vulkan::VulkanHelper::create_best_compute_device();
                vk_config = {
                    .instance = vk_objects.instance,
                    .physical_device = vk_objects.physical_device,
                    .device = vk_objects.device,
                    .compute_queue = vk_objects.compute_queue,
                    .transfer_queue = vk_objects.transfer_queue,
                    .allocate_memory_callback = nvigi::vulkan::default_allocate_memory,
                    .free_memory_callback = nvigi::vulkan::default_free_memory
                };
            } else if (parser.get("backend") == "d3d12") {
                deviceAndQueue = nvigi::d3d12::D3D12Helper::create_best_compute_device();
                d3d12_config = {
                    .device = deviceAndQueue.device.Get(),
                    .compute_queue = deviceAndQueue.compute_queue.Get(),
                    .create_committed_resource_callback = nvigi::d3d12::default_create_committed_resource,
                    .destroy_resource_callback = nvigi::d3d12::default_destroy_resource,
                    .create_resource_user_context = nullptr,
                    .destroy_resource_user_context = nullptr
                };
            }

            // Voice cloning / speaker / model parameters (qwen3tts only)
            nvigi::TTSVoiceCloneParameters voice_clone_params{};
            voice_clone_params.useLowMemModel = parser.has("low-mem");
            
            bool use_voice_clone = parser.has("ref-audio");
            bool use_speaker = parser.has("speaker");
            bool use_instruct = parser.has("instruct");
            std::string ref_audio_path_str;
            std::string ref_text_str;
            std::string speaker_str;
            std::string instruct_str;
            if (use_voice_clone) {
                ref_audio_path_str = parser.get("ref-audio").data();
                ref_text_str = parser.has("ref-text") ? std::string(parser.get("ref-text").data()) : "";
                voice_clone_params.refAudioPath = ref_audio_path_str.c_str();
                voice_clone_params.refText = ref_text_str.empty() ? nullptr : ref_text_str.c_str();
                voice_clone_params.mode = static_cast<nvigi::VoiceCloneMode>(parser.get_int("clone-mode"));
            }
            if (use_speaker) {
                speaker_str = parser.get("speaker").data();
                voice_clone_params.speaker = speaker_str.c_str();
            }
            if (use_instruct) {
                instruct_str = parser.get("instruct").data();
                voice_clone_params.instruct = instruct_str.c_str();
            }

            // Create TTS instance
            std::cout << "\n=== Creating TTS Instance ===\n";
            std::unique_ptr<Instance> instance;
            instance = Instance::create(
                ModelConfig{
                    .backend = parser.get("backend"),
                    .guid = parser.get("guid"),
                    .model_path = parser.get("models"),
                    .num_threads = parser.get_int("threads"),
                    .vram_budget_mb = parser.get_size_t("vram"),
                    .warm_up_models = true
                },
                d3d12_config,
                vk_config,
                core.loadInterface(),
                core.unloadInterface(),
                parser.get("plugin"),
                &voice_clone_params
            ).value();

            std::cout << "TTS instance created successfully!\n";
            std::cout << "\n=== NVIGI TTS Basic Sample ===\n";
            std::cout << "Backend: " << parser.get("backend") << "\n";
            std::cout << "Model GUID: " << parser.get("guid") << "\n";
            if (use_speaker) {
                std::cout << "Speaker: " << speaker_str << "\n";
            }
            if (use_voice_clone) {
                std::cout << "Voice Cloning: Enabled\n";
                std::cout << "  Reference Audio: " << ref_audio_path_str << "\n";
                std::cout << "  Clone Mode: " << (voice_clone_params.mode == nvigi::VoiceCloneMode::eICL ? "ICL" : "XVEC") << "\n";
                if (!ref_text_str.empty()) {
                    std::cout << "  Reference Text: " << ref_text_str << "\n";
                }
            }
            
            // Print supported languages
            auto supported_langs = instance->get_supported_languages();
            if (!supported_langs.empty()) {
                std::cout << "Supported Languages: ";
                for (size_t i = 0; i < supported_langs.size(); ++i) {
                    std::cout << supported_langs[i];
                    if (i < supported_langs.size() - 1) std::cout << ", ";
                }
                std::cout << "\n";
            }
            std::cout << "\n";

            std::string text = parser.get("text").data();
            std::string target_path = parser.get("target").data();
            std::string output_path = parser.get("output").data();
            float speed = parser.get_float("speed");
            std::string language = parser.get("language").data();
            int timesteps = parser.get_int("timesteps");
            bool play_audio = parser.has("play");

            std::cout << "Text: \"" << text << "\"\n";
            std::cout << "Target Voice: " << target_path << "\n";
            std::cout << "Output: " << output_path << "\n";
            std::cout << "Speed: " << speed << "x\n";
            std::cout << "Language: " << language << "\n";
            std::cout << "Timesteps: " << timesteps << "\n";
#ifdef NVIGI_WINDOWS
            std::cout << "Real-time Playback: " << (play_audio ? "Enabled" : "Disabled") << "\n";
#endif
            std::cout << "\n";

            // Configure runtime parameters
            auto config = RuntimeConfig{}
                .set_speed(speed)
                .set_language(language)
                .set_timesteps(timesteps)
                .set_flash_attention(true);

            const int bench_runs = parser.get_int("bench");
            if (bench_runs > 0) {
                std::cout << std::format("=== Bench Mode ({} iterations) ===\n", bench_runs);

                std::vector<float> rtf;
                for (int i = 0; i < bench_runs; ++i) {
                    const float run_rtf = generate_speech(instance, config, text, target_path);
                    if (run_rtf < 0.0f) {
                        return -1;
                    }

                    rtf.push_back(run_rtf);
                    std::cout << std::format("  run {}: RTF {:.2f}{}\n", i + 1, run_rtf,
                        i == 0 ? "  (warm-up, excluded below)" : "");
                }

                // Ignore first run (graph and compute buffer allocations)
                if (rtf.size() > 1) {
                    const float mean = std::accumulate(rtf.begin() + 1, rtf.end(), 0.0f) / (rtf.size() - 1);
                    std::cout << std::format("  steady-state RTF: mean {:.2f}, best {:.2f}\n",
                        mean, *std::min_element(rtf.begin() + 1, rtf.end()));
                }
                return 0;
            }

            bool use_async = parser.has("async");

            if (use_async) {
                std::cout << "=== Async Mode (Polled, Non-blocking) ===\n";
                std::cout << "Starting async speech generation...\n";

                // Start async operation
                auto op = instance->generate_async(text, target_path, config).value();

                // Create WAV writer
                WAVWriter wav_writer(output_path);
                if (!wav_writer.is_open()) {
                    std::cerr << "Failed to create output WAV file: " << output_path << "\n";
                    return -1;
                }

                size_t total_samples = 0;
                auto start_time = std::chrono::steady_clock::now();

                // Poll for results in a game-loop style
                std::cout << "Generating";
                std::cout.flush();

                while (!op.is_complete()) {
                    // Try to get results (non-blocking!)
                    if (auto result = op.try_get_results()) {
                        if (!result->audio.empty()) {
                            // Write audio chunk to file
                            wav_writer.write_samples(result->audio.data(), result->audio.size());
                            total_samples += result->audio.size();
                            
#ifdef NVIGI_WINDOWS
                            // Play audio in real-time if requested
                            if (play_audio) {
                                AudioPlayer::play_audio(result->audio.data(), result->audio.size());
                            }
#endif
                            
                            if (result->state == ExecutionState::DataPending) {
                                std::cout << "." << std::flush;
                            }
                        }
                        
                        if (result->state == ExecutionState::Done) {
                            std::cout << " Done!\n";
                        } else if (result->state == ExecutionState::Invalid) {
                            std::cerr << "\nError during speech generation!\n";
                            return -1;
                        }
                    }

                    // Simulate game loop - do other work here
                    // In a real game, you'd be rendering frames, updating physics, etc.
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                wav_writer.close();

                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                std::cout << "\n=== Generation Complete ===\n";
                std::cout << "Total Samples: " << total_samples << "\n";
                std::cout << "Duration: " << (total_samples / static_cast<float>(kSampleRate)) << " seconds\n";
                std::cout << "Generation Time: " << (duration.count() / 1000.0f) << " seconds\n";
                std::cout << "Output saved to: " << output_path << "\n";

            } else {
                std::cout << "=== Sync Mode (Blocking) ===\n";
                std::cout << "Starting speech generation...\n";

                // Create WAV writer
                WAVWriter wav_writer(output_path);
                if (!wav_writer.is_open()) {
                    std::cerr << "Failed to create output WAV file: " << output_path << "\n";
                    return -1;
                }

                size_t total_samples = 0;
                auto start_time = std::chrono::steady_clock::now();

                std::cout << "Generating";
                std::cout.flush();

                // Generate speech (blocking call)
                auto result = instance->generate(
                    text,
                    target_path,
                    config,
                    [&wav_writer, &total_samples, play_audio](const int16_t* audio, size_t samples, ExecutionState state) -> ExecutionState {
                        if (state == ExecutionState::DataPending || state == ExecutionState::Done) {
                            // Write audio chunk to file
                            wav_writer.write_samples(audio, samples);
                            total_samples += samples;
                            
#ifdef NVIGI_WINDOWS
                            // Play audio in real-time if requested
                            if (play_audio) {
                                AudioPlayer::play_audio(audio, samples);
                            }
#endif
                            
                            if (state == ExecutionState::DataPending) {
                                std::cout << "." << std::flush;
                            }
                        }
                        return state; // Continue normally
                    }
                );

                wav_writer.close();

                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                if (!result) {
                    std::cerr << "\nError: " << result.error().what() << "\n";
                    return -1;
                }

                std::cout << " Done!\n";
                std::cout << "\n=== Generation Complete ===\n";
                std::cout << "Total Samples: " << total_samples << "\n";
                std::cout << "Duration: " << (total_samples / static_cast<float>(kSampleRate)) << " seconds\n";
                std::cout << "Generation Time: " << (duration.count() / 1000.0f) << " seconds\n";
                std::cout << "Output saved to: " << output_path << "\n";
            }

            std::cout << "\n";
        }
        
        // Print memory statistics
        if (parser.get("backend") == "d3d12") {
            std::cout << "=== Memory Tracking Statistics ===\n";
            std::cout << std::format("D3D12 Resources Active: {}\n", nvigi::d3d12::g_resource_count.load());
            std::cout << std::format("D3D12 Total Bytes Allocated: {:.2f} MB\n", 
                nvigi::d3d12::g_total_allocation_bytes.load() / (1024.0 * 1024.0));
        } else if (parser.get("backend") == "vulkan") {
            std::cout << "=== Memory Tracking Statistics ===\n";
            std::cout << std::format("Vulkan Allocations Active: {}\n", nvigi::vulkan::g_memory_allocation_count.load());
            std::cout << std::format("Vulkan Total Bytes Allocated: {:.2f} MB\n", 
                nvigi::vulkan::g_total_allocation_bytes.load() / (1024.0 * 1024.0));
        }
    }
    catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }


 
    return 0;
}



