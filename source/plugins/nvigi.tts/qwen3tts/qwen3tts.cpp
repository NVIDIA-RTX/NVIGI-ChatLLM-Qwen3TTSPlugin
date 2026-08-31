// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "_artifacts/gitVersion.h"
#include "source/core/nvigi.api/nvigi.h"
#include "source/core/nvigi.exception/exception.h"
#include "source/core/nvigi.extra/extra.h"
#include "source/core/nvigi.log/log.h"
#include "source/core/nvigi.plugin/plugin.h"
#include "source/core/nvigi.system/system.h"
#include "source/plugins/nvigi.tts/qwen3tts/versions.h"
#include "source/plugins/nvigi.tts/nvigi_tts.h"
#include "source/utils/nvigi.ai/ai.h"
#include "nvigi_stl_helpers.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <nvigi_result.h>
#include <nvigi_struct.h>
#include <string.h>
#include <string>
#include <vector>

#include "libchatllm.h"

namespace nvigi
{

namespace qwen3tts
{

// chatllm_create hands back an object that must be released with chatllm_destroy.
struct ChatllmDeleter
{
    void operator()(chatllm_obj* obj) const
    {
        int destroyRc = chatllm_destroy(obj);
        if (destroyRc != 0)
        {
            NVIGI_LOG_ERROR("chatllm_destroy failed: %d (object still busy?)", destroyRc);
        }
    }
};
using ChatllmPtr = std::unique_ptr<chatllm_obj, ChatllmDeleter>;

struct InferenceContext
{
    // ChatLLM.cpp parses command line style arguments, each flag and value appended separately.
    void appendParam(const char* flag, const char* value)
    {
        chatllm_append_param(chatllm.get(), flag);
        chatllm_append_param(chatllm.get(), value);
    }

    ChatllmPtr chatllm;

    // ChatLLM owns these, so they are not valid at instance creation: audio_buffer, sample_rate and
    // channels are only populated once synthesize() has run and generation_complete is set by end_callback.
    std::vector<int16_t> audio_buffer;
    int sample_rate = 0;
    int channels = 0;
    bool generation_complete = false;

    // Set by us, from host-supplied creation parameters
    bool voiceCloneConfigured = false;
    TTSVoiceCloneParameters voiceCloneParams;
    json modelInfo;
};

PluginID getFeatureId(InferenceInstanceData *data)
{
    return plugin::tts::qwen3tts::kId;
}

// ChatLLM.cpp callback which maps onto NVIGI log levels.
static void print_callback(void* user_data, int print_type, const char* utf8_str) {
    switch (print_type) {
    case PRINTLN_META:
    case PRINTLN_MODEL_INFO:
        NVIGI_LOG_INFO("%s", utf8_str);
        break;
    case PRINTLN_ERROR:
        NVIGI_LOG_ERROR("%s", utf8_str);
        break;
    case PRINTLN_LOGGING:
        if (utf8_str && utf8_str[0] != ' ' && utf8_str[0] != '.') {
            NVIGI_LOG_VERBOSE("%s", utf8_str);
        }
        break;
    default:
        break;
    }
}

// ChatLLM.cpp callback which sets the generation_complete flag.
static void end_callback(void* user_data) {
    auto ctx = static_cast<InferenceContext*>(user_data);
    ctx->generation_complete = true;
}

// ChatLLM.cpp synthesizes audio from text and leaves the audio in the instance buffer.
static nvigi::Result synthesize(InferenceContext* instance, const char* text)
{
    if (!instance->chatllm) {
        NVIGI_LOG_ERROR("Model not initialized");
        return kResultInvalidState;
    }

    instance->generation_complete = false;

    int result = chatllm_speech_synthesis(instance->chatllm.get(), text);

    if (result != 0) {
        const char* errMsg = "Unknown error";
        if (result == -1) errMsg = "Model not loaded";
        else if (result == -2) errMsg = "Not a TTS model";
        else if (result == -3) errMsg = "Synthesis failed or returned no audio";
        NVIGI_LOG_ERROR("Failed to synthesize: %s (code %d)", errMsg, result);
        return kResultInvalidState;
    }

    int num_samples = 0;
    const int16_t* audio_ptr = (const int16_t*)chatllm_get_audio_data(instance->chatllm.get(), &num_samples);

    if (!audio_ptr || num_samples == 0) {
        NVIGI_LOG_ERROR("No audio data generated");
        return kResultInvalidState;
    }

    if (chatllm_get_audio_info(instance->chatllm.get(), &instance->sample_rate, &instance->channels) != 0) {
        NVIGI_LOG_ERROR("chatllm_get_audio_info failed");
        return kResultInvalidState;
    }

    instance->audio_buffer.assign(audio_ptr, audio_ptr + num_samples);

    NVIGI_LOG_VERBOSE("Synthesis completed: %d samples, %d Hz, %d channel(s)", num_samples, instance->sample_rate,
                      instance->channels);

    return kResultOk;
}

// Voice clone settings can only be applied after chatllm_start, because extracting the speaker
// embedding needs the model's speech tokenizer.
static nvigi::Result configureVoiceClone(InferenceContext* instance, const TTSVoiceCloneParameters* params)
{
    NVIGI_LOG_VERBOSE("Configuring voice clone");
    int cloneRc = chatllm_voice_clone_set_mode(instance->chatllm.get(), (int)params->mode);
    if (cloneRc != 0) {
        NVIGI_LOG_ERROR("chatllm_voice_clone_set_mode failed: %d (0=XVEC, 1=ICL)", cloneRc);
        return kResultInvalidParameter;
    }

    if (params->refAudioPath) {
        NVIGI_LOG_VERBOSE("Setting ref audio: %s", params->refAudioPath);
        cloneRc = chatllm_voice_clone_set_ref_audio(instance->chatllm.get(), params->refAudioPath);
        if (cloneRc != 0) {
            NVIGI_LOG_ERROR("chatllm_voice_clone_set_ref_audio failed: %d (file missing or invalid)", cloneRc);
            return kResultInvalidParameter;
        }
    } else if (params->refAudioData) {
        NVIGI_LOG_VERBOSE("Setting ref audio data: %d samples", params->refAudioNumSamples);
        cloneRc = chatllm_voice_clone_set_ref_audio_data(instance->chatllm.get(),
            params->refAudioData,
            params->refAudioNumSamples,
            params->refAudioSampleRate);
        if (cloneRc != 0) {
            NVIGI_LOG_ERROR("chatllm_voice_clone_set_ref_audio_data failed: %d", cloneRc);
            return kResultInvalidParameter;
        }
    }

    if (params->refText) {
        NVIGI_LOG_VERBOSE("Setting ref text: %s", params->refText);
        cloneRc = chatllm_voice_clone_set_ref_text(instance->chatllm.get(), params->refText);
        if (cloneRc != 0) {
            NVIGI_LOG_ERROR("chatllm_voice_clone_set_ref_text failed: %d", cloneRc);
            return kResultInvalidParameter;
        }
    }

    instance->voiceCloneConfigured = chatllm_voice_clone_is_ready(instance->chatllm.get()) == 1;
    NVIGI_LOG_INFO("Voice clone ready: %s", instance->voiceCloneConfigured ? "YES" : "NO");
    const bool cloneRequested = params->refAudioPath || params->refAudioData;
    if (cloneRequested && !instance->voiceCloneConfigured) {
        NVIGI_LOG_ERROR("Voice clone requested but not ready (XVEC needs ref audio; ICL also needs ref text)");
        return kResultInvalidParameter;
    }

    // Set speaker if specified (for CustomVoice model)
    if (params->speaker) {
        NVIGI_LOG_VERBOSE("Setting speaker: %s", params->speaker);
        cloneRc = chatllm_set_speaker(instance->chatllm.get(), params->speaker);
        if (cloneRc != 0) {
            NVIGI_LOG_ERROR("chatllm_set_speaker failed: %d", cloneRc);
            return kResultInvalidParameter;
        }
    }

    // Set instruct if specified (for VoiceDesign model)
    if (params->instruct) {
        NVIGI_LOG_VERBOSE("Setting instruct: %s", params->instruct);
        cloneRc = chatllm_set_instruct(instance->chatllm.get(), params->instruct);
        if (cloneRc != 0) {
            NVIGI_LOG_ERROR("chatllm_set_instruct failed: %d", cloneRc);
            return kResultInvalidParameter;
        }
    }

    return kResultOk;
}

const nvigi::InferenceDataDescriptorArray *getInputSignature(InferenceInstanceData *data)
{
    static std::vector<InferenceDataDescriptor> slots = {
        {nvigi::kTTSDataSlotInputText, InferenceDataText::s_type, false}};
    static InferenceDataDescriptorArray s_desc = {slots.size(), slots.data()};
    return &s_desc;
}

const nvigi::InferenceDataDescriptorArray *getOutputSignature(InferenceInstanceData *data)
{
    static std::vector<InferenceDataDescriptor> slots = {
        {nvigi::kTTSDataSlotOutputAudio, InferenceDataAudio::s_type, false}};
    static InferenceDataDescriptorArray s_desc = {slots.size(), slots.data()};
    return &s_desc;
}

struct Qwen3TTSContext
{
    NVIGI_PLUGIN_CONTEXT_CREATE_DESTROY(Qwen3TTSContext);

    void onCreateContext() {
        int result = chatllm_init();
        if (result != 0) {
            NVIGI_LOG_ERROR("chatllm_init failed: %d", result);
            return;
        }
        initialized = true;
    };

    void onDestroyContext() {};

    nvigi::PluginID feature{};

    ITextToSpeech api{};
    IPolledInferenceInterface polledApi{};

    std::atomic<bool> initialized = false;

    json modelInfo;
    ai::CommonCapsData capsData;
};

nvigi::Result evaluate(nvigi::InferenceExecutionContext *execCtx);
nvigi::Result evaluateAsync(nvigi::InferenceExecutionContext *execCtx);
nvigi::Result cancelAsyncEvaluation(nvigi::InferenceExecutionContext *execCtx);

} // namespace qwen3tts

//! IMPORTANT: Make sure to place this macro right after the context declaration and always within the 'nvigi' namespace
//! ONLY.
NVIGI_PLUGIN_DEFINE("nvigi.plugin.tts.qwen3tts", Version(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH),
                    Version(API_MAJOR, API_MINOR, API_PATCH), qwen3tts, Qwen3TTSContext)

// Brings up ChatLLM on 'instance': creates the object, picks a model for the requested GUID,
// applies runtime flags, starts it, and applies any voice clone settings. The try/catch guards the
// json conversions below - without it they would reach the SEH handler in createInstance's wrapper,
// which writes a minidump, and a malformed model config is user data rather than a crash.
static nvigi::Result loadAndStartModel(qwen3tts::InferenceContext* instance,
                                       const CommonCreationParameters* common,
                                       const nvigi::NVIGIParameter* _params)
{
    using namespace nvigi::qwen3tts;

    auto &ctx = (*getContext());
    if (!ctx.initialized) {
        NVIGI_LOG_ERROR("ChatLLM not initialized");
        return kResultInvalidState;
    }

    // Create ChatLLM object
    instance->chatllm.reset(chatllm_create());
    if (!instance->chatllm) {
        NVIGI_LOG_ERROR("Failed to create ChatLLM object");
        return kResultInvalidState;
    }

    // Find the bin models needed
    if (!ai::findModels(common, { "bin" }, instance->modelInfo))
    {
        return kResultInvalidParameter;
    }

    try
    {
        // Trim down to our GUID for this instance
        instance->modelInfo = instance->modelInfo[common->modelGUID];
        size_t neededVRAM = instance->modelInfo["vram"];
        if (common->vramBudgetMB < neededVRAM)
        {
            NVIGI_LOG_WARN("Provided VRAM %uMB is insufficient, required VRAM is %uMB", common->vramBudgetMB,
                neededVRAM);
            return kResultInsufficientResources;
        }

        std::vector<std::string> filesModels = instance->modelInfo["bin"];
        if (filesModels.empty())
        {
            NVIGI_LOG_ERROR("Failed to find model in the expected directory '%s'", common->utf8PathToModels);
            return kResultInvalidParameter;
        }

        std::string parentFolder = std::filesystem::path(filesModels[0]).parent_path().string();

        // Get voice clone parameters (also contains model selection)
        auto voiceCloneParams = findStruct<TTSVoiceCloneParameters>(_params);

        // Determine model size (VoiceDesign only available for 1.7B)
        bool useSmallModel = voiceCloneParams && voiceCloneParams->useLowMemModel;
        bool useVoiceDesign = voiceCloneParams && voiceCloneParams->instruct && !voiceCloneParams->refAudioPath;
        if (useVoiceDesign && useSmallModel) {
            NVIGI_LOG_WARN("VoiceDesign only available for 1.7B model, ignoring low-mem flag");
            useSmallModel = false;
        }
        const char* modelSizeStr = useSmallModel ? "0.6B" : "1.7B";

        // Determine model path
        std::string model_path;

        // Check for explicit model path first
        if (voiceCloneParams && voiceCloneParams->modelPath) {
            model_path = voiceCloneParams->modelPath;
            NVIGI_LOG_INFO("Using explicit model path");
        } else if (voiceCloneParams && voiceCloneParams->refAudioPath) {
            // Base model supports 3-second rapid voice clone from user audio input
            instance->voiceCloneParams = *voiceCloneParams;
            model_path = std::string(parentFolder) + "\\Qwen3-TTS-12Hz-" + modelSizeStr + "-Base-q8_0.bin";
            NVIGI_LOG_INFO("Voice clone ref audio found, using Base model (%s, supports voice cloning)", modelSizeStr);
            NVIGI_LOG_VERBOSE("refAudioPath: %s", voiceCloneParams->refAudioPath);
            NVIGI_LOG_VERBOSE("mode: %d", (int)voiceCloneParams->mode);
        } else if (useVoiceDesign) {
            // VoiceDesign model for custom voice from natural language description
            instance->voiceCloneParams = *voiceCloneParams;
            model_path = std::string(parentFolder) + "\\Qwen3-TTS-12Hz-1.7B-VoiceDesign-q8_0.bin";
            NVIGI_LOG_INFO("VoiceDesign mode, using VoiceDesign model (1.7B)");
            NVIGI_LOG_VERBOSE("instruct: %s", voiceCloneParams->instruct);
        } else {
            // CustomVoice model for standard TTS with pre-defined voices
            if (voiceCloneParams) {
                instance->voiceCloneParams = *voiceCloneParams;
            }
            model_path = std::string(parentFolder) + "\\Qwen3-TTS-12Hz-" + modelSizeStr + "-CustomVoice-q8_0.bin";
            NVIGI_LOG_INFO("Using CustomVoice model (%s)", modelSizeStr);
        }

        // Set model path
        NVIGI_LOG_INFO("Loading model: %s", model_path.c_str());
        instance->appendParam("-m", model_path.c_str());

        // Offload all layers to the GPU
        instance->appendParam("-ngl", "99");

        // Cap the context. ChatLLM otherwise sizes the KV cache from the model's own
        // max_length of 32768, which at 13 codec positions per second is ~42 minutes of
        // audio and costs 3.6GB of KV cache on the 1.7B models. 4096 tokens is ~5 minutes
        // of speech and keeps the fully offloaded instance inside the model card's budget.
        instance->appendParam("-l", "4096");

        // Start the model
        int result = chatllm_start(instance->chatllm.get(), print_callback, end_callback, instance);
        if (result != 0) {
            // 0 = ok; -1 = missing model path; >0 = parse stopped at that argv index;
            // 1 (EXIT_FAILURE) = exception while loading.
            NVIGI_LOG_ERROR("Failed to start model: %d", result);
            return kResultInvalidState;
        }

        NVIGI_LOG_INFO("Model initialized successfully");

        // Voice clone settings must wait until the model is loaded
        if (voiceCloneParams) {
            auto res = configureVoiceClone(instance, voiceCloneParams);
            if (res != kResultOk) {
                return res;
            }
        }
    }
    catch (std::exception& e)
    {
        NVIGI_LOG_ERROR("%s", e.what());
        return kResultInvalidState;
    }

    return kResultOk;
}

nvigi::Result qwen3ttsCreateInstance(const nvigi::NVIGIParameter *_params, nvigi::InferenceInstance **_instance)
{
    auto common = findStruct<CommonCreationParameters>(_params);
    auto creationParams = findStruct<TTSCreationParameters>(_params);
    if (!creationParams || !common)
    {
        NVIGI_LOG_ERROR("Missing required parameters");
        return nvigi::kResultInvalidParameter;
    }

    if (!_instance || !common->utf8PathToModels || !common->modelGUID)
    {
        NVIGI_LOG_ERROR("Missing instance pointer or pathToModels or modelGUID");
        return nvigi::kResultInvalidParameter;
    }

    if (!ai::isGuid(common->modelGUID))
    {
        NVIGI_LOG_ERROR("Provided model GUID '%s' is invalid", common->modelGUID);
        return kResultInvalidParameter;
    }

    *_instance = nullptr;

    // Owned here until it is handed to the InferenceInstance below, so failure paths just return.
    auto instanceData = std::make_unique<nvigi::qwen3tts::InferenceContext>();

#ifndef NVIGI_PRODUCTION
    size_t currentUsageMB{};
    extra::ScopedTasks vram(
        [&currentUsageMB]() {
            system::VRAMUsage* usage;
            system::getInterface()->getVRAMStats(0, &usage);
            currentUsageMB = usage->currentUsageMB;
        },
        [&currentUsageMB]() {
            system::VRAMUsage* usage;
            system::getInterface()->getVRAMStats(0, &usage);
            currentUsageMB = usage->currentUsageMB - currentUsageMB;
            NVIGI_LOG_INFO("New instance using %lluMB budget %lluMB", currentUsageMB, usage->budgetMB);
        });
#endif

    auto res = loadAndStartModel(instanceData.get(), common, _params);
    if (res != kResultOk)
    {
        return res;
    }

    auto instance = new InferenceInstance(kStructVersion1);
    instance->data = instanceData.release();
    instance->getFeatureId = qwen3tts::getFeatureId;
    instance->getInputSignature = qwen3tts::getInputSignature;
    instance->getOutputSignature = qwen3tts::getOutputSignature;
    instance->evaluate = qwen3tts::evaluate;
    instance->evaluateAsync = qwen3tts::evaluateAsync;
    instance->cancelAsyncEvaluation = qwen3tts::cancelAsyncEvaluation;

    *_instance = instance;

    return kResultOk;
}

nvigi::Result qwen3ttsGetCapsAndRequirements(nvigi::NVIGIParameter **_info, const nvigi::NVIGIParameter *_params)
{
    // population of the CapabilitiesAndRequirements is doing a promptData oriented population of
    // ALL models supported by this plugin.  It is encumbent on the user to iterate over these vectors
    // find the model they are interested in and the corresponding caps and requirements for that model.

    auto common = findStruct<CommonCreationParameters>(_params);
    if (!common)
    {
        return nvigi::kResultInvalidParameter;
    }

    if (!_info)
        return nvigi::kResultInvalidParameter;

    static CommonCapabilitiesAndRequirements s_capsCommon{};
    static TTSCapabilitiesAndRequirements s_caps{};
    s_caps.common = &s_capsCommon;
    auto info = &s_caps;
    *_info = s_caps;

    auto &ctx = (*qwen3tts::getContext());
    if (!ai::findModels(common, { "bin" }, ctx.modelInfo))
    {
        return kResultInvalidParameter;
    }

    // ChatLLM.cpp supports GPU acceleration
    info->common->supportedBackends = nvigi::InferenceBackendLocations::eGPU;
    //! Must be called after we set supported backends to correctly filter models
    ai::populateCommonCapsAndRequirements(ctx.capsData, *common, *info->common, ctx.modelInfo);

    return kResultOk;
}

// Copies into a host-provided output buffer. Bails if the buffer is missing or too small
static bool copyToHostBuffer(const CpuData* cpuBuffer, const void* src, size_t bytes)
{
    if (!cpuBuffer || !cpuBuffer->buffer || cpuBuffer->sizeInBytes < bytes)
    {
        return false;
    }
    memcpy_s(const_cast<void*>(cpuBuffer->buffer), cpuBuffer->sizeInBytes, src, bytes);
    return true;
}

nvigi::Result qwen3ttsEvaluate(nvigi::InferenceExecutionContext *execCtx)
{
    auto &ctx = (*qwen3tts::getContext());

    if (!execCtx)
    {
        NVIGI_LOG_ERROR("Unable to find 'InferenceExecutionContext' structure in the inputs");
        return kResultInvalidParameter;
    }

    if (!execCtx->callback)
    {
        NVIGI_LOG_ERROR("Qwen3TTS inference callback not provided");
        return kResultInvalidParameter;
    }

    if (!execCtx->instance)
    {
        NVIGI_LOG_ERROR("Qwen3TTS inference instance not provided");
        return kResultInvalidParameter;
    }

    if (ctx.feature != execCtx->instance->getFeatureId(execCtx->instance->data))
    {
        NVIGI_LOG_ERROR("Invalid inference instance - expecting Qwen3TTS %u got %u", ctx.feature,
                        execCtx->instance->getFeatureId(execCtx->instance->data));
        return kResultInvalidParameter;
    }

    using namespace nvigi::qwen3tts;
    auto instance = static_cast<nvigi::qwen3tts::InferenceContext *>(execCtx->instance->data);

    const nvigi::InferenceDataText *promptData{};
    if (!execCtx->inputs->findAndValidateSlot(kTTSDataSlotInputText, &promptData))
    {
        NVIGI_LOG_ERROR("Expecting single inference input of type 'nvigi::InferenceDataText'");
        return kResultInvalidParameter;
    }

    auto res = synthesize(instance, promptData->getUTF8Text());
    if (res != kResultOk)
    {
        return res;
    }

    std::vector<nvigi::InferenceDataSlot> slots;
    nvigi::InferenceDataByteArraySTLHelper dataAudio;

    // Output Audio - copy into the host's buffer when it provided one, otherwise hand back a slot
    // backed by 'dataAudio', which must outlive the callback below.
    const size_t audioBytes = instance->audio_buffer.size() * sizeof(int16_t);
    const nvigi::InferenceDataByteArray* hostAudio{};
    if (execCtx->outputs && execCtx->outputs->findAndValidateSlot(kTTSDataSlotOutputAudio, &hostAudio))
    {
        if (!copyToHostBuffer(castTo<CpuData>(hostAudio->bytes), instance->audio_buffer.data(), audioBytes))
        {
            return kResultInvalidState;
        }
    }
    else
    {
        dataAudio = InferenceDataByteArraySTLHelper(
            reinterpret_cast<const uint8_t*>(instance->audio_buffer.data()), audioBytes);
        slots.push_back(nvigi::InferenceDataSlot(kTTSDataSlotOutputAudio, dataAudio));
    }

    nvigi::InferenceDataSlotArray outputs;
    if (slots.size() > 0)
    {
        outputs = {slots.size(), slots.data()};
        execCtx->outputs = &outputs;
    }

    execCtx->callback(execCtx, nvigi::kInferenceExecutionStateDone, execCtx->callbackUserData);

    //! Clear outputs since these are all local variables
    execCtx->outputs = {};

    return nvigi::kResultOk;
}

nvigi::Result qwen3ttsDestroyInstance(const nvigi::InferenceInstance *instance)
{
    if (!instance)
        return kResultInvalidParameter;
    auto ttsInstance = static_cast<qwen3tts::InferenceContext *>(instance->data);

    // ~InferenceContext releases the ChatLLM object through ChatllmPtr
    delete ttsInstance;
    delete instance;

    return kResultOk;
}

// Async/polling not supported - ChatLLM.cpp TTS is synchronous only. These stubs cannot simply
// be dropped: the shared wrapper (samples/shared/cxx_wrappers/tts/tts.hpp) fails instance
// creation outright when the polled interface is absent, and calls evaluateAsync unguarded, so
// removing either breaks the sample even in synchronous mode.
Result qwen3ttsGetResults(InferenceExecutionContext*, bool, InferenceExecutionState*) {
    return kResultNoImplementation;
}

Result qwen3ttsReleaseResults(InferenceExecutionContext*, InferenceExecutionState) {
    return kResultNoImplementation;
}

Result qwen3ttsCancelAsyncEvaluation(InferenceExecutionContext*) {
    return kResultNoImplementation;
}

//! Making sure our implementation is covered with our exception handler
//!
namespace qwen3tts
{
nvigi::Result createInstance(const nvigi::NVIGIParameter *params, nvigi::InferenceInstance **instance)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsCreateInstance(params, instance));
}

nvigi::Result destroyInstance(const nvigi::InferenceInstance *instance)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsDestroyInstance(instance));
}

nvigi::Result getCapsAndRequirements(nvigi::NVIGIParameter **modelInfo, const nvigi::NVIGIParameter *params)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsGetCapsAndRequirements(modelInfo, params));
}
nvigi::Result evaluate(nvigi::InferenceExecutionContext *execCtx)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsEvaluate(execCtx));
}
nvigi::Result evaluateAsync(nvigi::InferenceExecutionContext*)
{
    return kResultNoImplementation;
}

nvigi::Result getResults(nvigi::InferenceExecutionContext* execCtx, bool wait, nvigi::InferenceExecutionState* state)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsGetResults(execCtx, wait, state));
}

nvigi::Result releaseResults(nvigi::InferenceExecutionContext* execCtx, nvigi::InferenceExecutionState state)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsReleaseResults(execCtx, state));
}

nvigi::Result cancelAsyncEvaluation(nvigi::InferenceExecutionContext* execCtx)
{
    NVIGI_CATCH_EXCEPTION(qwen3ttsCancelAsyncEvaluation(execCtx));
}

} // namespace qwen3tts

//! Main entry point - get information about our plugin
//!
Result nvigiPluginGetInfo(nvigi::framework::IFramework *framework, nvigi::plugin::PluginInfo **_info)
{
    if (!plugin::internalPluginSetup(framework))
        return kResultInvalidState;

    // Internal API, we know that incoming pointer is always valid
    auto &info = plugin::getContext()->info;
    *_info = &info;

    info.id = qwen3tts::getFeatureId(nullptr);
    info.description = "Qwen3 TTS plugin implementation";
    info.author = "NVIDIA";
    info.build = GIT_BRANCH_AND_LAST_COMMIT;
    info.interfaces = {plugin::getInterfaceInfo<ITextToSpeech>()};
    info.minDriver = {NVIGI_CUDA_MIN_DRIVER_MAJOR, NVIGI_CUDA_MIN_DRIVER_MINOR, NVIGI_CUDA_MIN_DRIVER_BUILD};
    info.minOS = {NVIGI_DEF_MIN_OS_MAJOR, NVIGI_DEF_MIN_OS_MINOR, NVIGI_DEF_MIN_OS_BUILD};
    info.minGPUArch = {NVIGI_CUDA_MIN_GPU_ARCH};
    info.requiredVendor = VendorId::eNVDA;

    return kResultOk;
}

//! Main entry point - starting our plugin
//!
Result nvigiPluginRegister(framework::IFramework *framework)
{
    if (!plugin::internalPluginSetup(framework))
        return kResultInvalidState;

    auto &ctx = (*qwen3tts::getContext());

    ctx.feature = qwen3tts::getFeatureId(nullptr);

    ctx.api.createInstance = qwen3tts::createInstance;
    ctx.api.destroyInstance = qwen3tts::destroyInstance;
    ctx.api.getCapsAndRequirements = qwen3tts::getCapsAndRequirements;

    framework->addInterface(ctx.feature, &ctx.api, 0);

    // Registered even though every entry point is unimplemented; see the note above the stubs.
    ctx.polledApi.getResults = qwen3tts::getResults;
    ctx.polledApi.releaseResults = qwen3tts::releaseResults;
    framework->addInterface(ctx.feature, &ctx.polledApi, 0);

    return kResultOk;
}

//! Main exit point - shutting down our plugin
//!
Result nvigiPluginDeregister()
{
    auto &ctx = (*qwen3tts::getContext());
    ai::freeCommonCapsAndRequirements(ctx.capsData);
    return kResultOk;
}

//! The only exported function - gateway to all functionality
NVIGI_EXPORT void *nvigiPluginGetFunction(const char *functionName)
{
    NVIGI_EXPORT_FUNCTION(nvigiPluginGetInfo);
    NVIGI_EXPORT_FUNCTION(nvigiPluginRegister);
    NVIGI_EXPORT_FUNCTION(nvigiPluginDeregister);

    return nullptr;
}

} // namespace nvigi
