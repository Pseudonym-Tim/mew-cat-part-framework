#ifndef MEW_CAT_PART_FRAMEWORK_H
#define MEW_CAT_PART_FRAMEWORK_H

#include <stdint.h>
#include <windows.h>

#define MOD_NAME "MewCatPartFramework"

#define RVA_PROCESS_ANIMATION_MERGES 0x009AE5D0U // Processes SWF animation merges so named-part bindings can be finalized after append work...
#define RVA_FIND_SWF_EXPORT 0x009ADC50U // Resolves SWF exports and intercepts annotated/synthetic texture append targets...
#define RVA_APPEND_MOVIE_CLIP 0x00A4E4D0U // Appends one MovieClip into another and records appended frame ranges/source definitions...
#define RVA_FIND_SWF_CHARACTER 0x00A3E6A0U // Looks up an SWF character definition by character ID...
#define RVA_GON_INDEX_BY_NAME_CONST 0x0093EA10U // Const GON field lookup used to resolve @named cat part references to numeric frames...
#define RVA_GON_INDEX_BY_NAME 0x0093EB10U // Mutable GON field lookup used to resolve @named cat part references to numeric frames...
#define RVA_CAT_PART_GRAPHICS_REFRESH 0x00739570U // Refreshes CatPartGraphics and synchronizes appended texture frames on first use...
#define RVA_MOVIE_CLIP_SET_FRAME 0x0099EC40U // Sets a MovieClip to a zero based frame after its backing texture definition is synchronized...

#define PROCESS_MERGES_HOOK_STOLEN_BYTES 15
#define FIND_EXPORT_HOOK_STOLEN_BYTES 15
#define APPEND_CLIP_HOOK_STOLEN_BYTES 15
#define GON_INDEX_HOOK_STOLEN_BYTES 15
#define CAT_PART_GRAPHICS_REFRESH_HOOK_STOLEN_BYTES 20

#define MAX_PATH_LENGTH 520
#define MAX_LINE_LENGTH 1024
#define MAX_PARTS 2048
#define MAX_BATCH_TARGETS 2048
#define MAX_PENDING_NAMED_FIELDS 2048
#define MAX_TEXTURE_APPEND_SOURCES 512
#define MAX_ID_LENGTH 128
#define MAX_TARGET_LENGTH 40
#define MAX_GON_STRING_LENGTH (1024U * 1024U)
#define BINDING_WAIT_YIELD_COUNT 4096
#define MAX_APPLICATION_SWFS 4096

// ApplicationBase/SWF layouts used for hidden textures...
#define APPLICATION_SWF_COUNT_OFFSET 0x114
#define APPLICATION_SWF_ARRAY_OFFSET 0x118
#define SWF_CHARACTER_TABLE_OFFSET 0x30
#define SWF_EXPORT_LIST_OFFSET 0x78
#define SWF_EXPORT_NODE_NAME_OFFSET 0x10
#define SWF_EXPORT_NODE_CHARACTER_ID_OFFSET 0x30
#define CAT_TEXTURE_PADDING_CHARACTER_ID 3917

#define MOVIE_CLIP_DATA_OFFSET 0x130
#define MOVIE_CLIP_INLINE_DATA_OFFSET 0x60
#define MOVIE_CLIP_FRAME_COUNT_OFFSET 0x00
#define MOVIE_CLIP_DATA_SIZE 0xD0
#define LIVE_MOVIE_CLIP_DEFINITION_OFFSET 0xD0
#define CAT_TEXTURE_ALIGNED_BASE_FRAMES 1506

#define CATPART_BODY_ID_OFFSET 0x030
#define CATPART_HEAD_ID_OFFSET 0x084
#define CATPART_TAIL_ID_OFFSET 0x0D8
#define CATPART_LEG1_ID_OFFSET 0x12C
#define CATPART_LEG2_ID_OFFSET 0x180
#define CATPART_ARM1_ID_OFFSET 0x1D4
#define CATPART_ARM2_ID_OFFSET 0x228
#define CATPART_LEFTEAR_ID_OFFSET 0x3CC
#define CATPART_RIGHTEAR_ID_OFFSET 0x420
#define CATPART_STATE_PREFIX_SIZE 0x004
#define CATPART_STATE_TEXTURE_OFFSET 0x008
#define CATPART_GRAPHICS_ENTRY_TOTAL_OFFSET 0x00C
#define CATPART_GRAPHICS_ENTRIES_OFFSET 0x010
#define CATPART_GRAPHICS_ENTRY_SIZE 0x030
#define CATPART_GRAPHICS_TEXTURE_CLIP_OFFSET 0x008

#define GON_INT_DATA_OFFSET 0x50
#define GON_FLOAT_DATA_OFFSET 0x58
#define GON_STRING_DATA_OFFSET 0x68
#define GON_TYPE_OFFSET 0xA8
#define GON_TYPE_STRING 1
#define GON_TYPE_NUMBER 2

#define MSVC_STRING_SIZE_OFFSET 0x10
#define MSVC_STRING_CAPACITY_OFFSET 0x18
#define MSVC_STRING_SSO_CAPACITY 15

__declspec(dllexport) int __cdecl
MewCatPartFramework_ResolvePart(
    const char* id,
    const char* expectedKind,
    int32_t* resolvedFrame);

__declspec(dllexport) int __cdecl
MewCatPartFramework_SyncTextureClip(
    const char* partKind,
    void* textureMovieClip);

#endif