#pragma once

#include <stdint.h>

#if defined(HEXENGINE_HOST_STATIC)
#define HEXENGINE_HOST_EXPORT
#elif defined(_WIN32)
#if defined(HEXENGINE_HOST_BUILD)
#define HEXENGINE_HOST_EXPORT __declspec(dllexport)
#else
#define HEXENGINE_HOST_EXPORT __declspec(dllimport)
#endif
#else
#define HEXENGINE_HOST_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* HexEngineRuntimeHandle;

HEXENGINE_HOST_EXPORT uint32_t hexengine_host_get_current_process_id(void);
HEXENGINE_HOST_EXPORT HexEngineRuntimeHandle hexengine_host_create_runtime_for_pid(uint32_t pid);
HEXENGINE_HOST_EXPORT void hexengine_host_destroy_runtime(HexEngineRuntimeHandle runtime);

HEXENGINE_HOST_EXPORT int hexengine_host_run_global(
    HexEngineRuntimeHandle runtime,
    const char* source,
    const char* chunk_name,
    char** out_json);

HEXENGINE_HOST_EXPORT int hexengine_host_run_script(
    HexEngineRuntimeHandle runtime,
    const char* script_id,
    const char* source,
    const char* chunk_name,
    char** out_json);

HEXENGINE_HOST_EXPORT int hexengine_host_destroy_script(
    HexEngineRuntimeHandle runtime,
    const char* script_id);

HEXENGINE_HOST_EXPORT const char* hexengine_host_get_last_error(void);
HEXENGINE_HOST_EXPORT void hexengine_host_free_string(char* value);

#ifdef __cplusplus
}
#endif
