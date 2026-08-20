/*
 * Mewjector API v3 header.
 * Resolved dynamically from version.dll, so no import library is needed.
 */
#ifndef MEWJECTOR_H
#define MEWJECTOR_H

#include <string.h>
#include <windows.h>

#define MJ_API_VERSION 3

typedef int (__cdecl *MJ_fn_InstallHook)(
    UINT_PTR rva,
    int stolenBytes,
    void* hookFn,
    void** outTrampoline,
    int priority,
    const char* owner
);
typedef int (__cdecl *MJ_fn_QueryHook)(UINT_PTR rva);
typedef UINT_PTR (__cdecl *MJ_fn_AllocTypeIdPair)(const char* owner);
typedef int (__cdecl *MJ_fn_RegisterName)(const char* category, const char* name, const char* owner);
typedef const char* (__cdecl *MJ_fn_LookupName)(const char* category, const char* name);
typedef UINT_PTR (__cdecl *MJ_fn_GetGameBase)(void);
typedef void (__cdecl *MJ_fn_Log)(const char* owner, const char* fmt, ...);
typedef int (__cdecl *MJ_fn_VerifyHooks)(void);
typedef int (__cdecl *MJ_fn_GetVersion)(void);

typedef struct
{
    MJ_fn_InstallHook InstallHook;
    MJ_fn_QueryHook QueryHook;
    MJ_fn_AllocTypeIdPair AllocTypeIdPair;
    MJ_fn_RegisterName RegisterName;
    MJ_fn_LookupName LookupName;
    MJ_fn_GetGameBase GetGameBase;
    MJ_fn_Log Log;
    MJ_fn_VerifyHooks VerifyHooks;
    MJ_fn_GetVersion GetVersion;
} MewjectorAPI;

static inline int MJ_Resolve(MewjectorAPI* api)
{
    HMODULE module;

    if (!api)
    {
        return 0;
    }

    memset(api, 0, sizeof(*api));
    module = GetModuleHandleA("version.dll");

    if (!module)
    {
        return 0;
    }

    api->GetVersion = (MJ_fn_GetVersion)GetProcAddress(module, "MJ_GetVersion");

    if (!api->GetVersion || api->GetVersion() < MJ_API_VERSION)
    {
        return 0;
    }

#define MJ_RESOLVE(field, exportName) \
    api->field = (MJ_fn_##field)GetProcAddress(module, "MJ_" exportName); \
    if (!api->field) return 0

    MJ_RESOLVE(InstallHook, "InstallHook");
    MJ_RESOLVE(QueryHook, "QueryHook");
    MJ_RESOLVE(AllocTypeIdPair, "AllocTypeIdPair");
    MJ_RESOLVE(RegisterName, "RegisterName");
    MJ_RESOLVE(LookupName, "LookupName");
    MJ_RESOLVE(GetGameBase, "GetGameBase");
    MJ_RESOLVE(Log, "Log");
    MJ_RESOLVE(VerifyHooks, "VerifyHooks");

#undef MJ_RESOLVE
    return 1;
}

#endif
