#include "MewCatPartFramework.h"
#include "mewjector.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* (__fastcall *fn_find_swf_export)(void* application, const void* name);
typedef void* (__fastcall *fn_find_swf_character)(void* characterTable, int32_t characterId);
typedef void (__fastcall *fn_append_movie_clip)(void* destination, void* source);
typedef void* (__fastcall *fn_gon_index_by_name)(void* gonObject, const void* fieldName);
typedef void (__fastcall *fn_process_animation_merges)(void* application, void* swf);
typedef void (__fastcall *fn_cat_part_graphics_refresh)(void* graphics, void* partState, void* catParts, int32_t palette);
typedef void (__fastcall *fn_movie_clip_set_frame)(void* movieClip, int32_t zeroBasedFrame);

typedef struct
{
    char id[MAX_ID_LENGTH];
    char kind[16];
    char batch[MAX_ID_LENGTH];
    char sourcePath[MAX_PATH_LENGTH];
    int32_t logicalIndex;
    int32_t duplicate;
} PartDefinition;

typedef struct
{
    char batch[MAX_ID_LENGTH];
    char target[MAX_TARGET_LENGTH];
    int32_t baseFrame;
    int32_t appendedFrames;
    int32_t duplicate;
} BatchTarget;

typedef struct
{
    int active;
    int recordBatch;
    int textureTargetIndex;
    void* destination;
    char batch[MAX_ID_LENGTH];
    char target[MAX_TARGET_LENGTH];
} PendingAppend;

typedef struct
{
    void* source;
} TextureAppendSource;

#define TEXTURE_SYNC_CACHE_SLOTS 256

typedef struct
{
    void* volatile definition;
    volatile LONG sourceCount;
    volatile LONG finalFrames;
} TextureSyncCacheEntry;

typedef struct
{
    uint8_t prefix[MOVIE_CLIP_DATA_OFFSET];
    void* data;
} MovieClipAppendShim;

typedef struct
{
    void* field;
    char id[MAX_ID_LENGTH];
    char kind[16];
} PendingNamedField;

typedef struct
{
    union
    {
        char small[16];
        const char* pointer;
    } data;
    size_t size;
    size_t capacity;
} BorrowedMsvcString;

typedef struct
{
    const char* target;
    const char* alias;
    const char* parentExport;
    int32_t expectedParentCharacterId;
    int32_t hiddenTextureCharacterId;
    int32_t minimumBaseFrames;
} TextureTargetDefinition;

static const char* const BODY_TARGETS[] = {"CatBody"};
static const char* const HEAD_TARGETS[] = {"CatHead"};
static const char* const LEG_TARGETS[] = {"CatLeg"};
static const char* const TAIL_TARGETS[] = {"CatTail"};
static const char* const EAR_TARGETS[] = {"CatEar"};
static const char* const EYE_TARGETS[] = {"CatEye", "CatEye_Right", "CatEyeClosed", "CatEyeClosed_Right"};
static const char* const EYEBROW_TARGETS[] = {"CatEyebrow"};
static const char* const MOUTH_TARGETS[] = {"CatMouth", "CatMouthOpen", "CatMouthSmile"};
static const char* const TEXTURE_TARGETS[] = {
    "CatBodyTexture",
    "CatHeadTexture",
    "CatLegTexture",
    "CatTailTexture",
    "CatEarTexture"
};

static const TextureTargetDefinition TEXTURE_TARGET_DEFINITIONS[] = {
    {"CatBodyTexture", "CatBodyTex", "CatBody", 8845, 8053, 1506},
    {"CatHeadTexture", "CatHeadTex", "CatHead", 7132, 6291, 1505},
    {"CatLegTexture", "CatLegTex", "CatLeg", 9925, 9053, 1506},
    {"CatTailTexture", "CatTailTex", "CatTail", 10982, 10120, 1506},
    {"CatEarTexture", "CatEarTex", "CatEar", 8051, 7268, 1506}
};

static MewjectorAPI g_mj;
static HMODULE g_moduleHandle;
static fn_process_animation_merges g_origProcessAnimationMerges;
static fn_find_swf_export g_origFindSwfExport;
static fn_find_swf_character g_findSwfCharacter;
static fn_append_movie_clip g_origAppendMovieClip;
static fn_gon_index_by_name g_origGonIndexByNameConst;
static fn_gon_index_by_name g_origGonIndexByName;
static fn_cat_part_graphics_refresh g_origCatPartGraphicsRefresh;
static fn_movie_clip_set_frame g_setMovieClipFrame;
static PartDefinition g_parts[MAX_PARTS];
static BatchTarget g_batchTargets[MAX_BATCH_TARGETS];
static PendingNamedField g_pendingNamedFields[MAX_PENDING_NAMED_FIELDS];
static int32_t g_partCount;
static int32_t g_batchTargetCount;
static int32_t g_pendingNamedFieldCount;
static volatile LONG g_manifestsLoaded;

static volatile LONG g_hookInstallState;
static volatile LONG g_activeAnimationMerges;
static volatile LONG g_activeAnnotatedAppends;
static CRITICAL_SECTION g_registryLock;
static CRITICAL_SECTION g_pendingFieldLock;
static CRITICAL_SECTION g_textureAlignmentLock;
static void* g_textureAlignedSwf;
static void* g_texturePaddingCharacter;
static TextureAppendSource g_textureAppendSources[5][MAX_TEXTURE_APPEND_SOURCES];
static int32_t g_textureAppendSourceCounts[5];
static volatile LONG g_textureSyncFailureLogged[5];
static TextureSyncCacheEntry g_textureSyncCache[5][TEXTURE_SYNC_CACHE_SLOTS];
static __declspec(thread) PendingAppend g_pendingAppend;
static __declspec(thread) int32_t g_insideBindingWork;

static void RetryPendingNamedFields(void);

static void Log(const char* format, ...)
{
    char buffer[1024];
    va_list args;

    if (!g_mj.Log)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    g_mj.Log(MOD_NAME, "%s", buffer);
}

static char* TrimInPlace(char* text)
{
    char* end;

    while (text && *text && isspace((unsigned char)*text))
    {
        ++text;
    }

    if (!text)
    {
        return NULL;
    }

    end = text + strlen(text);

    while (end > text && isspace((unsigned char)end[-1]))
    {
        --end;
    }

    *end = '\0';
    return text;
}

static char* StripUtf8Bom(char* text)
{
    unsigned char* bytes = (unsigned char*)text;

    if (bytes && bytes[0] == 0xEFU && bytes[1] == 0xBBU && bytes[2] == 0xBFU)
    {
        return text + 3;
    }

    return text;
}

static int IsMemoryRangeAccessible(const void* address, size_t length, int requireWrite)
{
    MEMORY_BASIC_INFORMATION memory;
    UINT_PTR begin;
    UINT_PTR end;
    UINT_PTR regionEnd;
    DWORD protection;

    if (!address || length == 0U)
    {
        return 0;
    }

    begin = (UINT_PTR)address;
    end = begin + length;

    if (end < begin || VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0U)
    {
        return 0;
    }

    regionEnd = (UINT_PTR)memory.BaseAddress + memory.RegionSize;

    if (regionEnd < (UINT_PTR)memory.BaseAddress || end > regionEnd)
    {
        return 0;
    }

    if (!requireWrite)
    {
        return 1;
    }

    protection = memory.Protect & 0xFFU;
    return protection == PAGE_READWRITE ||  protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

static int IsValidId(const char* id)
{
    const unsigned char* cursor = (const unsigned char*)id;

    if (!cursor || *cursor == '\0')
    {
        return 0;
    }

    while (*cursor)
    {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-' && *cursor != '.')
        {
            return 0;
        }

        ++cursor;
    }

    return 1;
}

static int GetMsvcStringView(const void* stringObject, const char** text, size_t* length)
{
    const uint8_t* bytes = (const uint8_t*)stringObject;
    const char* data;
    size_t size;
    size_t capacity;

    if (!bytes || !text || !length || !IsMemoryRangeAccessible(bytes, MSVC_STRING_CAPACITY_OFFSET + sizeof(capacity), 0))
    {
        return 0;
    }

    memcpy(&size, bytes + MSVC_STRING_SIZE_OFFSET, sizeof(size));
    memcpy(&capacity, bytes + MSVC_STRING_CAPACITY_OFFSET, sizeof(capacity));

    if (size > MAX_GON_STRING_LENGTH || capacity < size)
    {
        return 0;
    }

    if (capacity <= MSVC_STRING_SSO_CAPACITY)
    {
        data = (const char*)bytes;
    }
    else
    {
        memcpy(&data, bytes, sizeof(data));
    }

    if (!data || (size != 0U && !IsMemoryRangeAccessible(data, size, 0)))
    {
        return 0;
    }

    *text = data;
    *length = size;
    return 1;
}

static int MsvcStringEquals(const void* stringObject, const char* literal)
{
    const char* text;
    size_t length;
    size_t literalLength = strlen(literal);

    return GetMsvcStringView(stringObject, &text, &length) && length == literalLength && memcmp(text, literal, length) == 0;
}

static void MakeBorrowedMsvcString(BorrowedMsvcString* value, const char* text)
{
    size_t length = strlen(text);

    memset(value, 0, sizeof(*value));
    value->size = length;

    if (length <= MSVC_STRING_SSO_CAPACITY)
    {
        memcpy(value->data.small, text, length + 1U);
        value->capacity = MSVC_STRING_SSO_CAPACITY;
    }
    else
    {
        value->data.pointer = text;
        value->capacity = length;
    }
}

static const char* CanonicalKind(const char* kind)
{
    if (_stricmp(kind, "body") == 0) return "body";
    if (_stricmp(kind, "head") == 0) return "head";
    if (_stricmp(kind, "leg") == 0 || _stricmp(kind, "legs") == 0) return "leg";
    if (_stricmp(kind, "tail") == 0) return "tail";
    if (_stricmp(kind, "ear") == 0 || _stricmp(kind, "ears") == 0) return "ear";
    if (_stricmp(kind, "eye") == 0 || _stricmp(kind, "eyes") == 0) return "eye";
    if (_stricmp(kind, "eyebrow") == 0 || _stricmp(kind, "eyebrows") == 0 || _stricmp(kind, "brow") == 0 || _stricmp(kind, "brows") == 0) return "eyebrow";
    if (_stricmp(kind, "mouth") == 0 || _stricmp(kind, "mouths") == 0) return "mouth";
    if (_stricmp(kind, "texture") == 0 || _stricmp(kind, "textures") == 0) return "texture";
    
    return NULL;
}

static int StringViewEqualsLiteral(const char* text, size_t length, const char* literal)
{
    size_t literalLength = strlen(literal);
    return length == literalLength && memcmp(text, literal, length) == 0;
}

static const char* KindForGonField(const void* fieldName)
{
    const char* text;
    size_t length;

    /*
    * This hook sits on a global engine GON lookup. Read
    * the view once, then do cheap length/memcmp dispatch...
    */
    if (!GetMsvcStringView(fieldName, &text, &length))
    {
        return NULL;
    }

    if (StringViewEqualsLiteral(text, length, "body")) return "body";
    if (StringViewEqualsLiteral(text, length, "head")) return "head";
    if (StringViewEqualsLiteral(text, length, "tail")) return "tail";
    if (StringViewEqualsLiteral(text, length, "leg1") || StringViewEqualsLiteral(text, length, "leg2") || StringViewEqualsLiteral(text, length, "arm1") || StringViewEqualsLiteral(text, length, "arm2")) return "leg";
    if (StringViewEqualsLiteral(text, length, "lefteye") || StringViewEqualsLiteral(text, length, "righteye")) return "eye";
    if (StringViewEqualsLiteral(text, length, "lefteyebrow") || StringViewEqualsLiteral(text, length, "righteyebrow")) return "eyebrow";
    if (StringViewEqualsLiteral(text, length, "leftear") || StringViewEqualsLiteral(text, length, "rightear")) return "ear";
    if (StringViewEqualsLiteral(text, length, "mouth")) return "mouth";
    if (StringViewEqualsLiteral(text, length, "texture")) return "texture";

    return NULL;
}

static void GetRequiredTargets(const char* kind, const char* const** targets, int32_t* targetCount)
{
    if (strcmp(kind, "body") == 0)
    {
        *targets = BODY_TARGETS; *targetCount = 1;
    }
    else if (strcmp(kind, "head") == 0)
    {
        *targets = HEAD_TARGETS; *targetCount = 1;
    }
    else if (strcmp(kind, "leg") == 0)
    {
        *targets = LEG_TARGETS; *targetCount = 1;
    }
    else if (strcmp(kind, "tail") == 0)
    {
        *targets = TAIL_TARGETS; *targetCount = 1;
    }
    else if (strcmp(kind, "ear") == 0)
    {
        *targets = EAR_TARGETS; *targetCount = 1;
    }
    else if (strcmp(kind, "eye") == 0)
    {
        *targets = EYE_TARGETS; *targetCount = 4;
    }
    else if (strcmp(kind, "eyebrow") == 0)
    {
        *targets = EYEBROW_TARGETS; *targetCount = 1;
    }
    else if (strcmp(kind, "mouth") == 0)
    {
        *targets = MOUTH_TARGETS; *targetCount = 3;
    }
    else
    {
        *targets = TEXTURE_TARGETS; *targetCount = 5;
    }
}

static int ParseManifestLine(char* line, const char* sourcePath, int32_t lineNumber)
{
    char* equals;
    char* left;
    char* right;
    char* context;
    char* token;
    char* canonicalKind;
    char* end;
    long logicalIndex;
    PartDefinition part;

    line = StripUtf8Bom(TrimInPlace(line));

    if (!line || *line == '\0')
    {
        return 0;
    }

    equals = strchr(line, '=');

    if (!equals)
    {
        Log("%s:%d: expected '='", sourcePath, lineNumber);
        return 0;
    }

    *equals = '\0';
    left = TrimInPlace(line);
    right = TrimInPlace(equals + 1);
    memset(&part, 0, sizeof(part));

    if (!IsValidId(left) || strlen(left) >= sizeof(part.id))
    {
        Log("%s:%d: invalid or overlong part ID", sourcePath, lineNumber);
        return 0;
    }

    snprintf(part.id, sizeof(part.id), "%s", left);
    snprintf(part.sourcePath, sizeof(part.sourcePath), "%s", sourcePath);
    context = NULL;
    token = strtok_s(right, " \t,", &context);
    canonicalKind = token ? (char*)CanonicalKind(token) : NULL;

    if (!canonicalKind)
    {
        Log("%s:%d: unknown part kind", sourcePath, lineNumber);
        return 0;
    }

    snprintf(part.kind, sizeof(part.kind), "%s", canonicalKind);
    token = strtok_s(NULL, " \t,", &context);

    if (!token || !IsValidId(token) || strlen(token) >= sizeof(part.batch))
    {
        Log("%s:%d: missing or invalid append batch ID", sourcePath, lineNumber);
        return 0;
    }

    snprintf(part.batch, sizeof(part.batch), "%s", token);

    token = strtok_s(NULL, " \t,", &context);

    if (!token)
    {
        Log("%s:%d: missing logical part index", sourcePath, lineNumber);
        return 0;
    }

    logicalIndex = strtol(token, &end, 10);

    if (*end != '\0' || logicalIndex < 1 || logicalIndex > INT32_MAX)
    {
        Log("%s:%d: logical part index must be a positive integer", sourcePath, lineNumber);
        return 0;
    }

    if (strtok_s(NULL, " \t,", &context) != NULL)
    {
        Log("%s:%d: unexpected data after logical part index", sourcePath, lineNumber);
        return 0;
    }

    part.logicalIndex = (int32_t)logicalIndex;

    if (g_partCount >= MAX_PARTS)
    {
        Log("Part registry is full; skipped %s", part.id);
        return 0;
    }

    g_parts[g_partCount++] = part;
    return 1;
}

static void LoadManifest(const char* path)
{
    FILE* file = fopen(path, "rb");
    char line[MAX_LINE_LENGTH];
    int32_t lineNumber = 0;
    unsigned char bom[2];

    if (!file)
    {
        return;
    }

    if (fread(bom, 1U, 2U, file) == 2U && ((bom[0] == 0xFFU && bom[1] == 0xFEU) || (bom[0] == 0xFEU && bom[1] == 0xFFU)))
    {
        Log("UTF-16 manifest is unsupported: %s", path);
        fclose(file);
        return;
    }

    fseek(file, 0L, SEEK_SET);
    Log("Reading %s", path);

    while (fgets(line, sizeof(line), file))
    {
        char* comment;
        ++lineNumber;
        comment = strstr(line, "//");

        if (comment)
        {
            *comment = '\0';
        }

        ParseManifestLine(line, path, lineNumber);
    }

    fclose(file);
}

static void GetDirectoryFromPath(const char* path, char* output, size_t outputSize)
{
    const char* slash = strrchr(path, '\\');
    size_t length;

    if (!slash)
    {
        output[0] = '\0';
        return;
    }

    length = (size_t)(slash - path);

    if (length >= outputSize)
    {
        length = outputSize - 1U;
    }

    memcpy(output, path, length);
    output[length] = '\0';
}

static void GetFileNameFromPath(const char* path, char* output, size_t outputSize)
{
    const char* slash = strrchr(path, '\\');
    snprintf(output, outputSize, "%s", slash ? slash + 1 : path);
}

static int CompareParts(const void* left, const void* right)
{
    const PartDefinition* a = (const PartDefinition*)left;
    const PartDefinition* b = (const PartDefinition*)right;
    int result = _stricmp(a->id, b->id);

    if (result == 0)
    {
        result = _stricmp(a->sourcePath, b->sourcePath);
    }

    return result;
}

static void FinalizeParts(void)
{
    int32_t index;

    qsort(g_parts, (size_t)g_partCount, sizeof(g_parts[0]), CompareParts);

    for (index = 1; index < g_partCount; ++index)
    {
        if (_stricmp(g_parts[index - 1].id, g_parts[index].id) == 0)
        {
            g_parts[index].duplicate = 1;
            Log("Duplicate @%s ignored from %s, winner is %s", g_parts[index].id, g_parts[index].sourcePath, g_parts[index - 1].sourcePath);
        }
    }

    for (index = 0; index < g_partCount; ++index)
    {
        if (!g_parts[index].duplicate && g_mj.RegisterName && !g_mj.RegisterName("cat-part", g_parts[index].id, MOD_NAME))
        {
            Log("Name collision for cat part @%s", g_parts[index].id);
        }
    }
}

static void ScanSiblingManifests(void)
{
    char modulePath[MAX_PATH_LENGTH];
    char frameworkDirectory[MAX_PATH_LENGTH];
    char frameworkFolder[MAX_PATH_LENGTH];
    char modsDirectory[MAX_PATH_LENGTH];
    char searchPath[MAX_PATH_LENGTH];
    WIN32_FIND_DATAA findData;
    HANDLE findHandle;
    char* slash;

    if (!GetModuleFileNameA(g_moduleHandle, modulePath, (DWORD)sizeof(modulePath)))
    {
        Log("Could not determine framework DLL path!");
        return;
    }

    GetDirectoryFromPath(modulePath, frameworkDirectory, sizeof(frameworkDirectory));
    GetFileNameFromPath(frameworkDirectory, frameworkFolder, sizeof(frameworkFolder));
    snprintf(modsDirectory, sizeof(modsDirectory), "%s", frameworkDirectory);
    slash = strrchr(modsDirectory, '\\');

    if (!slash)
    {
        return;
    }

    *slash = '\0';
    snprintf(searchPath, sizeof(searchPath), "%s\\*", modsDirectory);
    findHandle = FindFirstFileA(searchPath, &findData);

    if (findHandle == INVALID_HANDLE_VALUE)
    {
        Log("Could not enumerate sibling mod folders!");
        return;
    }

    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U && strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0 && _stricmp(findData.cFileName, frameworkFolder) != 0)
        {
            char manifestPath[MAX_PATH_LENGTH];
            snprintf(manifestPath, sizeof(manifestPath), "%s\\%s\\cat_parts.txt", modsDirectory, findData.cFileName);

            if (GetFileAttributesA(manifestPath) != INVALID_FILE_ATTRIBUTES)
            {
                LoadManifest(manifestPath);
            }
        }
    } while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);

    snprintf(searchPath, sizeof(searchPath), "%s\\cat_parts.txt", frameworkDirectory);

    if (GetFileAttributesA(searchPath) != INVALID_FILE_ATTRIBUTES)
    {
        LoadManifest(searchPath);
    }
}

static void EnsureManifestsLoaded(void)
{
    LONG state = InterlockedCompareExchange(&g_manifestsLoaded, 1, 0);

    if (state == 0)
    {
        ScanSiblingManifests();
        FinalizeParts();
        MemoryBarrier();
        InterlockedExchange(&g_manifestsLoaded, 2);
        Log("Part manifest scan complete: %d definitions", g_partCount);
        return;
    }

    while (InterlockedCompareExchange(&g_manifestsLoaded, 2, 2) == 1)
    {
        Sleep(0);
    }
}

static int ParseAnnotatedExport(const void* nameObject, char* target, size_t targetSize, char* batch, size_t batchSize)
{
    static const char marker[] = "__MCPF__";
    const size_t markerLength = sizeof(marker) - 1U;
    const char* text;
    const char* markerAt = NULL;
    size_t index;
    size_t length;
    size_t targetLength;
    size_t batchLength;

    if (!GetMsvcStringView(nameObject, &text, &length))
    {
        return 0;
    }

    if (length < markerLength)
    {
        return 0;
    }

    for (index = 0; index + markerLength <= length; ++index)
    {
        if (memcmp(text + index, marker, markerLength) == 0)
        {
            markerAt = text + index;
            break;
        }
    }

    if (!markerAt)
    {
        return 0;
    }

    targetLength = (size_t)(markerAt - text);
    batchLength = length - targetLength - markerLength;

    if (targetLength == 0U || targetLength >= targetSize || batchLength == 0U || batchLength >= batchSize)
    {
        return 0;
    }

    memcpy(target, text, targetLength);
    target[targetLength] = '\0';
    memcpy(batch, markerAt + markerLength, batchLength);
    batch[batchLength] = '\0';

    return IsValidId(batch);
}

static int32_t MovieClipFrameCount(void* clip)
{
    uint8_t* bytes = (uint8_t*)clip;
    void* data;
    int32_t frameCount;

    if (!bytes || !IsMemoryRangeAccessible(bytes, MOVIE_CLIP_DATA_OFFSET + sizeof(data), 0))
    {
        return -1;
    }

    memcpy(&data, bytes + MOVIE_CLIP_DATA_OFFSET, sizeof(data));

    if (!data)
    {
        data = bytes + MOVIE_CLIP_INLINE_DATA_OFFSET;
    }

    if (!IsMemoryRangeAccessible(data, MOVIE_CLIP_FRAME_COUNT_OFFSET + sizeof(frameCount), 0))
    {
        return -1;
    }

    memcpy(&frameCount, (uint8_t*)data + MOVIE_CLIP_FRAME_COUNT_OFFSET, sizeof(frameCount));
    return frameCount;
}

static int32_t MovieClipDataFrameCount(void* data)
{
    int32_t frameCount;

    if (!data || !IsMemoryRangeAccessible(data, MOVIE_CLIP_FRAME_COUNT_OFFSET + sizeof(frameCount), 0))
    {
        return -1;
    }

    memcpy(&frameCount, (uint8_t*)data + MOVIE_CLIP_FRAME_COUNT_OFFSET, sizeof(frameCount));

    return frameCount;
}

static int TextureTargetIndexFromDefinition(const TextureTargetDefinition* definition)
{
    size_t index;

    if (!definition)
    {
        return -1;
    }

    for (index = 0; index < sizeof(TEXTURE_TARGET_DEFINITIONS) / sizeof(TEXTURE_TARGET_DEFINITIONS[0]); ++index)
    {
        if (definition == &TEXTURE_TARGET_DEFINITIONS[index])
        {
            return (int)index;
        }
    }

    return -1;
}

static int TextureTargetIndexForPartKind(const char* partKind)
{
    if (!partKind)
    {
        return -1;
    }

    if (_stricmp(partKind, "body") == 0)
    {
        return 0;
    }

    if (_stricmp(partKind, "head") == 0)
    {
        return 1;
    }

    if (_stricmp(partKind, "leg") == 0)
    {
        return 2;
    }

    if (_stricmp(partKind, "tail") == 0)
    {
        return 3;
    }

    if (_stricmp(partKind, "ear") == 0)
    {
        return 4;
    }

    return -1;
}

static int AppendToMovieClipData(void* destinationData, void* source)
{
    MovieClipAppendShim shim;

    if (!destinationData || !source || !g_origAppendMovieClip)
    {
        return 0;
    }

    memset(&shim, 0, sizeof(shim));
    shim.data = destinationData;
    g_origAppendMovieClip(&shim, source);
    return 1;
}

static void RecordTextureAppendSource(int textureTargetIndex, void* source)
{
    int32_t sourceIndex;
    TextureAppendSource* added;

    if (textureTargetIndex < 0 || textureTargetIndex >= 5 || !source)
    {
        return;
    }

    EnterCriticalSection(&g_textureAlignmentLock);
    sourceIndex = g_textureAppendSourceCounts[textureTargetIndex];

    if (sourceIndex >= MAX_TEXTURE_APPEND_SOURCES)
    {
        LeaveCriticalSection(&g_textureAlignmentLock);
        Log("Texture append source registry is full; cannot mirror %s", TEXTURE_TARGET_DEFINITIONS[textureTargetIndex].target);
        return;
    }

    added = &g_textureAppendSources[textureTargetIndex][sourceIndex];
    added->source = source;
    g_textureAppendSourceCounts[textureTargetIndex] = sourceIndex + 1;
    LeaveCriticalSection(&g_textureAlignmentLock);
}

static int ShouldLogTextureSyncFailure(int textureTargetIndex)
{
    if (textureTargetIndex < 0 || textureTargetIndex >= 5)
    {
        return 0;
    }

    return InterlockedCompareExchange(&g_textureSyncFailureLogged[textureTargetIndex], 1, 0) == 0;
}

static const TextureTargetDefinition* FindTextureTargetDefinition(const char* target)
{
    size_t index;

    if (!target)
    {
        return NULL;
    }

    for (index = 0; index < sizeof(TEXTURE_TARGET_DEFINITIONS) / sizeof(TEXTURE_TARGET_DEFINITIONS[0]); ++index)
    {
        const TextureTargetDefinition* definition = &TEXTURE_TARGET_DEFINITIONS[index];

        if (_stricmp(target, definition->target) == 0 || (definition->alias && _stricmp(target, definition->alias) == 0))
        {
            return definition;
        }
    }

    return NULL;
}

static const TextureTargetDefinition* FindTextureTargetDefinitionFromName(const void* nameObject)
{
    const char* text;
    size_t length;
    size_t index;

    if (!GetMsvcStringView(nameObject, &text, &length))
    {
        return NULL;
    }

    for (index = 0; index < sizeof(TEXTURE_TARGET_DEFINITIONS) / sizeof(TEXTURE_TARGET_DEFINITIONS[0]); ++index)
    {
        const TextureTargetDefinition* definition = &TEXTURE_TARGET_DEFINITIONS[index];
        size_t targetLength = strlen(definition->target);
        size_t aliasLength = definition->alias ? strlen(definition->alias) : 0U;

        if ((length == targetLength && memcmp(text, definition->target, length) == 0) || (definition->alias && length == aliasLength && memcmp(text, definition->alias, length) == 0))
        {
            return definition;
        }
    }

    return NULL;
}

static int EnsureBaseTextureTimelinesAligned(void* swf)
{
    void* textures[5];
    void* filler;
    int32_t frames[5];
    int32_t index;
    int result = 0;

    if (!swf || !g_findSwfCharacter || !g_origAppendMovieClip)
    {
        return 0;
    }

    EnterCriticalSection(&g_textureAlignmentLock);

    if (g_textureAlignedSwf == swf)
    {
        LeaveCriticalSection(&g_textureAlignmentLock);
        return 1;
    }

    for (index = 0; index < 5; ++index)
    {
        textures[index] = g_findSwfCharacter((uint8_t*)swf + SWF_CHARACTER_TABLE_OFFSET, TEXTURE_TARGET_DEFINITIONS[index].hiddenTextureCharacterId);
        frames[index] = MovieClipFrameCount(textures[index]);

        if (!textures[index] || frames[index] < TEXTURE_TARGET_DEFINITIONS[index].minimumBaseFrames)
        {
            Log("Cannot initialize texture appends: %s hidden character %d is invalid", TEXTURE_TARGET_DEFINITIONS[index].target, TEXTURE_TARGET_DEFINITIONS[index].hiddenTextureCharacterId);
            goto done;
        }
    }

    filler = g_findSwfCharacter((uint8_t*)swf + SWF_CHARACTER_TABLE_OFFSET, CAT_TEXTURE_PADDING_CHARACTER_ID);
    
    if (!filler || MovieClipFrameCount(filler) != 1)
    {
        Log("Cannot initialize texture appends: empty padding character %d is invalid", CAT_TEXTURE_PADDING_CHARACTER_ID);
        goto done;
    }

    g_texturePaddingCharacter = filler;

    if (frames[0] == frames[1] && frames[0] == frames[2] && frames[0] == frames[3] && frames[0] == frames[4])
    {
        result = 1;
        goto done;
    }

    if (frames[0] != frames[1] + 1 || frames[2] != frames[0] || frames[3] != frames[0] || frames[4] != frames[0])
    {
        Log("Cannot initialize texture appends: Hidden texture frame counts are Body=%d Head=%d Leg=%d Tail=%d Ear=%d", frames[0], frames[1], frames[2], frames[3], frames[4]);
        goto done;
    }

    g_origAppendMovieClip(textures[1], filler);

    if (MovieClipFrameCount(textures[1]) != frames[0])
    {
        Log("Cannot initialize texture appends: CatHeadTexture padding append failed");
        goto done;
    }

    Log("Aligned hidden cat texture timelines at frame %d using vanilla MovieClip append", frames[0]);
    result = 1;

done:
    if (result)
    {
        g_textureAlignedSwf = swf;
    }

    LeaveCriticalSection(&g_textureAlignmentLock);
    return result;
}

static int SwfFindExportCharacterId(void* swf, const char* exportName, int32_t* characterId)
{
    uint8_t* bytes = (uint8_t*)swf;
    void* sentinel;
    void* node;
    int32_t iterations;

    if (!swf || !exportName || !characterId || !IsMemoryRangeAccessible(bytes + SWF_EXPORT_LIST_OFFSET, sizeof(sentinel), 0))
    {
        return 0;
    }

    memcpy(&sentinel, bytes + SWF_EXPORT_LIST_OFFSET, sizeof(sentinel));

    if (!sentinel || !IsMemoryRangeAccessible(sentinel, sizeof(node), 0))
    {
        return 0;
    }

    memcpy(&node, sentinel, sizeof(node));

    for (iterations = 0; node && node != sentinel && iterations < 4096; ++iterations)
    {
        uint8_t* nodeBytes = (uint8_t*)node;
        int32_t id;
        void* next;

        if (!IsMemoryRangeAccessible(node, SWF_EXPORT_NODE_CHARACTER_ID_OFFSET + sizeof(id), 0))
        {
            return 0;
        }

        if (MsvcStringEquals(nodeBytes + SWF_EXPORT_NODE_NAME_OFFSET, exportName))
        {
            memcpy(&id, nodeBytes + SWF_EXPORT_NODE_CHARACTER_ID_OFFSET, sizeof(id));
            
            if (id <= 0)
            {
                return 0;
            }

            *characterId = id;
            return 1;
        }

        memcpy(&next, node, sizeof(next));
        node = next;
    }

    return 0;
}

static void* FindHiddenTextureDestination(void* application, const TextureTargetDefinition* definition)
{
    uint8_t* bytes = (uint8_t*)application;
    BorrowedMsvcString parentName;
    void* expectedParent;
    void** swfs;
    int32_t swfCount;
    int32_t index;

    if (!application || !definition || !g_findSwfCharacter || !IsMemoryRangeAccessible(bytes + APPLICATION_SWF_COUNT_OFFSET, APPLICATION_SWF_ARRAY_OFFSET - APPLICATION_SWF_COUNT_OFFSET + sizeof(swfs), 0))
    {
        return NULL;
    }

    MakeBorrowedMsvcString(&parentName, definition->parentExport);
    expectedParent = g_origFindSwfExport ? g_origFindSwfExport(application, &parentName) : NULL;
    
    if (!expectedParent)
    {
        Log("Cannot route %s: exported parent %s is unavailable", definition->target, definition->parentExport);
        return NULL;
    }

    memcpy(&swfCount, bytes + APPLICATION_SWF_COUNT_OFFSET, sizeof(swfCount));
    memcpy(&swfs, bytes + APPLICATION_SWF_ARRAY_OFFSET, sizeof(swfs));

    if (swfCount <= 0 || swfCount > MAX_APPLICATION_SWFS || !swfs || !IsMemoryRangeAccessible(swfs, (size_t)swfCount * sizeof(*swfs), 0))
    {
        return NULL;
    }

    // Match ApplicationBase::find_swf_export: (Newest loaded SWF wins)...
    for (index = swfCount - 1; index >= 0; --index)
    {
        void* swf = swfs[index];
        int32_t parentCharacterId;
        void* candidateParent;
        void* hiddenTexture;
        int32_t hiddenFrames;

        if (!SwfFindExportCharacterId(swf, definition->parentExport, &parentCharacterId))
        {
            continue;
        }

        candidateParent = g_findSwfCharacter((uint8_t*)swf + SWF_CHARACTER_TABLE_OFFSET, parentCharacterId);
        
        if (candidateParent != expectedParent)
        {
            continue;
        }

        if (parentCharacterId != definition->expectedParentCharacterId)
        {
            Log("Cannot route %s: newest %s export has character ID %d, expected %d", definition->target, definition->parentExport, parentCharacterId, definition->expectedParentCharacterId);
            return NULL;
        }

        if (!EnsureBaseTextureTimelinesAligned(swf))
        {
            return NULL;
        }

        hiddenTexture = g_findSwfCharacter((uint8_t*)swf + SWF_CHARACTER_TABLE_OFFSET, definition->hiddenTextureCharacterId);
        hiddenFrames = MovieClipFrameCount(hiddenTexture);

        if (!hiddenTexture || hiddenFrames < definition->minimumBaseFrames)
        {
            Log("Cannot route %s: hidden texture character %d under %s is invalid", definition->target, definition->hiddenTextureCharacterId, definition->parentExport);
            return NULL;
        }

        return hiddenTexture;
    }

    Log("Cannot route %s: no loaded %s export owns the expected catparts.swf layout", definition->target, definition->parentExport);
    return NULL;
}

static BatchTarget* FindBatchTarget(const char* batch, const char* target)
{
    int32_t index;

    for (index = 0; index < g_batchTargetCount; ++index)
    {
        if (_stricmp(g_batchTargets[index].batch, batch) == 0 && _stricmp(g_batchTargets[index].target, target) == 0)
        {
            return &g_batchTargets[index];
        }
    }

    return NULL;
}

static void RecordBatchTarget(const char* batch, const char* target, int32_t baseFrame, int32_t appendedFrames)
{
    BatchTarget* existing;
    BatchTarget* added;

    EnterCriticalSection(&g_registryLock);
    existing = FindBatchTarget(batch, target);

    if (existing)
    {
        existing->duplicate = 1;
        Log("Duplicate append batch target: %s / %s; named parts in this batch are disabled", batch, target);
        LeaveCriticalSection(&g_registryLock);
        return;
    }

    if (g_batchTargetCount >= MAX_BATCH_TARGETS)
    {
        Log("Append batch registry is full, skipped %s / %s", batch, target);
        LeaveCriticalSection(&g_registryLock);
        return;
    }

    added = &g_batchTargets[g_batchTargetCount++];
    memset(added, 0, sizeof(*added));
    snprintf(added->batch, sizeof(added->batch), "%s", batch);
    snprintf(added->target, sizeof(added->target), "%s", target);
    added->baseFrame = baseFrame;
    added->appendedFrames = appendedFrames;
    Log("Bound batch %s / %s to frames %d..%d", batch, target, baseFrame + 1, baseFrame + appendedFrames);
    LeaveCriticalSection(&g_registryLock);
}

static void* __fastcall HookFindSwfExport(void* application, const void* name)
{
    BorrowedMsvcString baseName;
    char target[MAX_TARGET_LENGTH];
    char batch[MAX_ID_LENGTH];
    const TextureTargetDefinition* textureTarget;
    void* result;

    memset(&g_pendingAppend, 0, sizeof(g_pendingAppend));
    g_pendingAppend.textureTargetIndex = -1;

    if (ParseAnnotatedExport(name, target, sizeof(target), batch, sizeof(batch)))
    {
        textureTarget = FindTextureTargetDefinition(target);

        if (textureTarget)
        {
            result = FindHiddenTextureDestination(application, textureTarget);

            if (result)
            {
                g_pendingAppend.active = 1;
                g_pendingAppend.recordBatch = 1;
                g_pendingAppend.textureTargetIndex = TextureTargetIndexFromDefinition(textureTarget);
                g_pendingAppend.destination = result;
                snprintf(g_pendingAppend.batch, sizeof(g_pendingAppend.batch), "%s", batch);
                snprintf(g_pendingAppend.target, sizeof(g_pendingAppend.target), "%s", textureTarget->target);
            }

            return result;
        }

        MakeBorrowedMsvcString(&baseName, target);
        result = g_origFindSwfExport ? g_origFindSwfExport(application, &baseName) : NULL;

        if (result)
        {
            g_pendingAppend.active = 1;
            g_pendingAppend.recordBatch = 1;
            g_pendingAppend.destination = result;
            snprintf(g_pendingAppend.batch, sizeof(g_pendingAppend.batch), "%s", batch);
            snprintf(g_pendingAppend.target, sizeof(g_pendingAppend.target), "%s", target);
        }

        return result;
    }

    /*
    * Plain synthetic names intentionally work without __MCPF__. This makes
    * _Append_CatBodyTexture/_Append_CatHeadTexture/etc. behave like
    * native append targets for authors who want to use numeric texture IDs
    * and otherwise stay entirely within the game's stock merge flow...
    */
    textureTarget = FindTextureTargetDefinitionFromName(name);

    if (textureTarget)
    {
        result = FindHiddenTextureDestination(application, textureTarget);

        if (result)
        {
            g_pendingAppend.active = 1;
            g_pendingAppend.recordBatch = 0;
            g_pendingAppend.textureTargetIndex = TextureTargetIndexFromDefinition(textureTarget);
            g_pendingAppend.destination = result;
            snprintf(g_pendingAppend.target, sizeof(g_pendingAppend.target), "%s", textureTarget->target);
        }

        return result;
    }

    return g_origFindSwfExport ? g_origFindSwfExport(application, name) : NULL;
}

static void __fastcall HookAppendMovieClip(void* destination, void* source)
{
    PendingAppend pending = g_pendingAppend;
    int32_t baseFrame = -1;
    int32_t appendedFrames = -1;
    int recordBatch = 0;

    memset(&g_pendingAppend, 0, sizeof(g_pendingAppend));

    if (pending.active && pending.destination == destination)
    {
        baseFrame = MovieClipFrameCount(destination);
        appendedFrames = MovieClipFrameCount(source);
        recordBatch = pending.recordBatch;
    }

    /*
    * Keep the replay registry independent of RecordBatchTarget's storage.
    * The live replay only needs the persistent source definition, its frame
    * extent is re-read from that definition when synchronization runs.
    * Recording it here also prevents later batch bookkeeping from becoming
    * part of the texture-source state...
    */
    if (baseFrame >= 0 && appendedFrames > 0 && pending.textureTargetIndex >= 0)
    {
        RecordTextureAppendSource(pending.textureTargetIndex, source);
    }

    /*
    * Publish the observed named-batch range before the append executes. A
    * GON lookup may run concurrently on another game thread while the append
    * is in progress, the active-operation counters below make that resolver
    * wait until the MovieClip mutation has committed...
    */
    if (recordBatch && baseFrame >= 0 && appendedFrames > 0)
    {
        InterlockedIncrement(&g_activeAnnotatedAppends);
        ++g_insideBindingWork;
        RecordBatchTarget(pending.batch, pending.target, baseFrame, appendedFrames);
    }

    if (g_origAppendMovieClip)
    {
        g_origAppendMovieClip(destination, source);
    }

    if (recordBatch && baseFrame >= 0 && appendedFrames > 0)
    {
        --g_insideBindingWork;

        if (InterlockedDecrement(&g_activeAnnotatedAppends) == 0 && InterlockedCompareExchange(&g_activeAnimationMerges, 0, 0) == 0)
        {
            RetryPendingNamedFields();
        }
    }
}

static void __fastcall HookProcessAnimationMerges(void* application, void* swf)
{
    LONG state;

    /*
    * This hook is installed before the find/append hooks. If the
    * game reaches an SWF merge on another thread during installation, hold
    * that merge until both halves are callable...
    */
    do
    {
        state = InterlockedCompareExchange(&g_hookInstallState, 0, 0);

        if (state == 1)
        {
            Sleep(0);
        }
    } while (state == 1);

    if (g_origProcessAnimationMerges)
    {
        InterlockedIncrement(&g_activeAnimationMerges);
        ++g_insideBindingWork;
        g_origProcessAnimationMerges(application, swf);
        --g_insideBindingWork;

        if (InterlockedDecrement(&g_activeAnimationMerges) == 0 && InterlockedCompareExchange(&g_activeAnnotatedAppends, 0, 0) == 0)
        {
            RetryPendingNamedFields();
        }
    }
}

static PartDefinition* FindPart(const char* id)
{
    int32_t low = 0;
    int32_t high = g_partCount - 1;

    while (low <= high)
    {
        int32_t middle = low + (high - low) / 2;
        int result = _stricmp(id, g_parts[middle].id);

        if (result == 0)
        {
            while (middle > 0 && _stricmp(id, g_parts[middle - 1].id) == 0)
            {
                --middle;
            }

            return g_parts[middle].duplicate ? NULL : &g_parts[middle];
        }

        if (result < 0)
        {
            high = middle - 1;
        }
        else
        {
            low = middle + 1;
        }
    }

    return NULL;
}

static int ResolvePartFrame(const PartDefinition* part, int32_t* resolvedFrame)
{
    const char* const* requiredTargets;
    int32_t requiredTargetCount;
    int32_t index;
    int32_t sharedFirstFrame = INT32_MIN;
    int32_t sharedLastFrame = INT32_MAX;
    int64_t resolved;

    GetRequiredTargets(part->kind, &requiredTargets, &requiredTargetCount);

    EnterCriticalSection(&g_registryLock);

    for (index = 0; index < requiredTargetCount; ++index)
    {
        BatchTarget* mapping = FindBatchTarget(part->batch, requiredTargets[index]);
        int32_t firstFrame;
        int32_t lastFrame;

        if (!mapping || mapping->duplicate || mapping->appendedFrames < 1)
        {
            LeaveCriticalSection(&g_registryLock);
            return 0;
        }

        firstFrame = mapping->baseFrame + 1;
        lastFrame = mapping->baseFrame + mapping->appendedFrames;

        if (firstFrame > sharedFirstFrame)
        {
            sharedFirstFrame = firstFrame;
        }

        if (lastFrame < sharedLastFrame)
        {
            sharedLastFrame = lastFrame;
        }
    }

    LeaveCriticalSection(&g_registryLock);
    resolved = (int64_t)sharedFirstFrame + (int64_t)part->logicalIndex - 1;

    if (sharedFirstFrame > sharedLastFrame || resolved < sharedFirstFrame || resolved > sharedLastFrame || resolved > INT32_MAX)
    {
        Log("@%s logical index %d is outside the shared appended range %d..%d", part->id, part->logicalIndex, sharedFirstFrame, sharedLastFrame);
        return 0;
    }

    *resolvedFrame = (int32_t)resolved;
    return 1;
}

static int BindingWorkIsActive(void)
{
    return InterlockedCompareExchange(&g_activeAnimationMerges, 0, 0) != 0 || InterlockedCompareExchange(&g_activeAnnotatedAppends, 0, 0) != 0;
}

static int ResolvePartFrameAfterActiveBindings(const PartDefinition* part, int32_t* resolvedFrame)
{
    int32_t yields;

    if (ResolvePartFrame(part, resolvedFrame))
    {
        return 1;
    }

    if (g_insideBindingWork != 0)
    {
        return 0;
    }

    for (yields = 0; yields < BINDING_WAIT_YIELD_COUNT && BindingWorkIsActive(); ++yields)
    {
        Sleep(0);
    }

    return ResolvePartFrame(part, resolvedFrame);
}

static int ReadNamedPartId(void* field, char* id, size_t idSize)
{
    uint8_t* bytes = (uint8_t*)field;
    const char* token;
    size_t tokenLength;
    int32_t fieldType;

    if (!field || !id || idSize < 2U || !IsMemoryRangeAccessible(field, GON_TYPE_OFFSET + sizeof(fieldType), 0))
    {
        return 0;
    }

    memcpy(&fieldType, bytes + GON_TYPE_OFFSET, sizeof(fieldType));
    
    if (fieldType != GON_TYPE_STRING || !GetMsvcStringView(bytes + GON_STRING_DATA_OFFSET, &token, &tokenLength) || tokenLength < 2U || tokenLength >= idSize || token[0] != '@')
    {
        return 0;
    }

    memcpy(id, token + 1, tokenLength - 1U);
    id[tokenLength - 1U] = '\0';
    return IsValidId(id);
}

static int ApplyResolvedPartFrame(void* field, int32_t frame)
{
    uint8_t* bytes = (uint8_t*)field;
    int32_t fieldType = GON_TYPE_NUMBER;
    double frameAsDouble = (double)frame;

    if (!IsMemoryRangeAccessible(field, GON_TYPE_OFFSET + sizeof(fieldType), 1))
    {
        return 0;
    }

    memcpy(bytes + GON_INT_DATA_OFFSET, &frame, sizeof(frame));
    memcpy(bytes + GON_FLOAT_DATA_OFFSET, &frameAsDouble, sizeof(frameAsDouble));
    MemoryBarrier();
    memcpy(bytes + GON_TYPE_OFFSET, &fieldType, sizeof(fieldType));
    return 1;
}

static void QueuePendingNamedField(void* field, const char* id, const char* kind)
{
    int32_t index;
    PendingNamedField* pending;

    EnterCriticalSection(&g_pendingFieldLock);

    for (index = 0; index < g_pendingNamedFieldCount; ++index)
    {
        pending = &g_pendingNamedFields[index];

        if (pending->field == field)
        {
            snprintf(pending->id, sizeof(pending->id), "%s", id);
            snprintf(pending->kind, sizeof(pending->kind), "%s", kind);
            LeaveCriticalSection(&g_pendingFieldLock);
            return;
        }
    }

    if (g_pendingNamedFieldCount >= MAX_PENDING_NAMED_FIELDS)
    {
        LeaveCriticalSection(&g_pendingFieldLock);
        Log("Pending named cat-part registry is full, skipped @%s", id);
        return;
    }

    pending = &g_pendingNamedFields[g_pendingNamedFieldCount++];
    memset(pending, 0, sizeof(*pending));
    pending->field = field;
    snprintf(pending->id, sizeof(pending->id), "%s", id);
    snprintf(pending->kind, sizeof(pending->kind), "%s", kind);
    LeaveCriticalSection(&g_pendingFieldLock);
    Log("Deferred @%s until its %s append mappings are ready", id, kind);
}

/*
* Returns 1 after applying the frame, 0 while the batch is still incomplete,
* and -1 when the queued field is stale or permanently invalid...
*/
static int RetryPendingNamedField(const PendingNamedField* pending)
{
    char currentId[MAX_ID_LENGTH];
    PartDefinition* part;
    int32_t frame;

    if (!ReadNamedPartId(pending->field, currentId, sizeof(currentId)) || _stricmp(currentId, pending->id) != 0)
    {
        return -1;
    }

    EnsureManifestsLoaded();
    part = FindPart(currentId);

    if (!part || strcmp(part->kind, pending->kind) != 0)
    {
        return -1;
    }

    if (!ResolvePartFrame(part, &frame))
    {
        return 0;
    }

    if (!ApplyResolvedPartFrame(pending->field, frame))
    {
        return -1;
    }

    Log("Resolved deferred @%s => frame %d", currentId, frame);
    return 1;
}

static void RetryPendingNamedFields(void)
{
    PendingNamedField pending;
    int32_t index = 0;
    int status;

    while (1)
    {
        EnterCriticalSection(&g_pendingFieldLock);

        if (index >= g_pendingNamedFieldCount)
        {
            LeaveCriticalSection(&g_pendingFieldLock);
            break;
        }

        pending = g_pendingNamedFields[index];
        LeaveCriticalSection(&g_pendingFieldLock);

        status = RetryPendingNamedField(&pending);

        EnterCriticalSection(&g_pendingFieldLock);

        if (index < g_pendingNamedFieldCount && g_pendingNamedFields[index].field == pending.field && _stricmp(g_pendingNamedFields[index].id, pending.id) == 0)
        {
            if (status != 0)
            {
                --g_pendingNamedFieldCount;
                g_pendingNamedFields[index] = g_pendingNamedFields[g_pendingNamedFieldCount];
            }
            else
            {
                ++index;
            }
        }

        LeaveCriticalSection(&g_pendingFieldLock);
    }
}

__declspec(dllexport) int __cdecl
MewCatPartFramework_ResolvePart(const char* id, const char* expectedKind, int32_t* resolvedFrame)
{
    PartDefinition* part;

    if (!id || !expectedKind || !resolvedFrame)
    {
        return 0;
    }

    if (*id == '@')
    {
        ++id;
    }

    if (!IsValidId(id))
    {
        return 0;
    }

    EnsureManifestsLoaded();
    part = FindPart(id);

    if (!part || strcmp(part->kind, expectedKind) != 0)
    {
        return 0;
    }

    return ResolvePartFrameAfterActiveBindings(part, resolvedFrame);
}

static TextureSyncCacheEntry* TextureSyncCacheSlot(int targetIndex, const void* definition)
{
    UINT_PTR value;

    if (targetIndex < 0 || targetIndex >= 5 || !definition)
    {
        return NULL;
    }

    value = (UINT_PTR)definition;
    value ^= value >> 17;
    value ^= value >> 31;
    return &g_textureSyncCache[targetIndex][(value >> 4) & (TEXTURE_SYNC_CACHE_SLOTS - 1)];
}

static int TextureSyncCacheHit(int targetIndex, void* definition, int32_t sourceCount)
{
    TextureSyncCacheEntry* slot = TextureSyncCacheSlot(targetIndex, definition);
    int32_t frameCount;

    if (!slot || slot->definition != definition || slot->sourceCount != sourceCount)
    {
        return 0;
    }

    memcpy(&frameCount, (uint8_t*)definition + MOVIE_CLIP_FRAME_COUNT_OFFSET, sizeof(frameCount));
    return frameCount == slot->finalFrames;
}

static void RememberTextureSyncCache(int targetIndex, void* definition, int32_t sourceCount, int32_t finalFrames)
{
    TextureSyncCacheEntry* slot = TextureSyncCacheSlot(targetIndex, definition);

    if (!slot)
    {
        return;
    }

    slot->sourceCount = sourceCount;
    slot->finalFrames = finalFrames;
    MemoryBarrier();
    slot->definition = definition;
    MemoryBarrier();
}

__declspec(dllexport) int __cdecl
MewCatPartFramework_SyncTextureClip(const char* partKind, void* textureMovieClip)
{
    uint8_t* liveClip = (uint8_t*)textureMovieClip;
    void* definitionData;
    int targetIndex;
    int32_t currentFrames;
    int32_t expectedFrames;
    int32_t sourceCount;
    int32_t sourceIndex;
    int32_t applied = 0;

    targetIndex = TextureTargetIndexForPartKind(partKind);

    if (targetIndex < 0 || !textureMovieClip || !g_origAppendMovieClip || !IsMemoryRangeAccessible(liveClip, LIVE_MOVIE_CLIP_DEFINITION_OFFSET + sizeof(definitionData), 0))
    {
        return 0;
    }

    /* 
    * Validate the live instance once, then let already-validated persistent
    * SWF definitions bypass the definition checks, lock and source walk. 
    */
    memcpy(&definitionData, liveClip + LIVE_MOVIE_CLIP_DEFINITION_OFFSET, sizeof(definitionData));
    sourceCount = g_textureAppendSourceCounts[targetIndex];
    
    if (definitionData && TextureSyncCacheHit(targetIndex, definitionData, sourceCount))
    {
        return 1;
    }

    if (!definitionData || !IsMemoryRangeAccessible(definitionData, MOVIE_CLIP_DATA_SIZE, 1))
    {
        return 0;
    }

    EnterCriticalSection(&g_textureAlignmentLock);
    currentFrames = MovieClipDataFrameCount(definitionData);

    if (currentFrames < 1)
    {
        LeaveCriticalSection(&g_textureAlignmentLock);
        return 0;
    }

    /*
    * Custom Head symbol can embed the unmodified tex
    * timeline, which is one frame shorter than the other four families.
    * Mirror the same one frame bootstrap used by the base hidden target...
    */
    if (targetIndex == 1 && currentFrames == TEXTURE_TARGET_DEFINITIONS[1].minimumBaseFrames)
    {
        if (!g_texturePaddingCharacter || !AppendToMovieClipData(definitionData, g_texturePaddingCharacter))
        {
            LeaveCriticalSection(&g_textureAlignmentLock);
            return 0;
        }

        currentFrames = MovieClipDataFrameCount(definitionData);

        if (currentFrames != CAT_TEXTURE_ALIGNED_BASE_FRAMES)
        {
            LeaveCriticalSection(&g_textureAlignmentLock);
            return 0;
        }

        ++applied;
    }

    if (currentFrames < CAT_TEXTURE_ALIGNED_BASE_FRAMES)
    {
        LeaveCriticalSection(&g_textureAlignmentLock);
        return 0;
    }

    expectedFrames = CAT_TEXTURE_ALIGNED_BASE_FRAMES;

    for (sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
    {
        TextureAppendSource* source = &g_textureAppendSources[targetIndex][sourceIndex];
        int32_t sourceFrames;
        int32_t nextExpected;

        /*
        * Do not cache this integer beside the source pointer. The observed
        * merge-time count is useful for batch binding, but live replay can
        * safely derive immutable source extent from the loaded SWF
        * character itself. (This also avoids coupling replay state to the
        * BatchTarget registry used by named-part resolution)...
        */
        sourceFrames = MovieClipFrameCount(source->source);

        if (!source->source || sourceFrames < 1 || expectedFrames > INT32_MAX - sourceFrames)
        {
            if (ShouldLogTextureSyncFailure(targetIndex))
            {
                Log( "Cannot sync live %s tex timeline: texture append source %d is no longer readable", partKind, sourceIndex);
            }

            LeaveCriticalSection(&g_textureAlignmentLock);
            return 0;
        }

        nextExpected = expectedFrames + sourceFrames;

        if (currentFrames == expectedFrames)
        {
            if (!AppendToMovieClipData(definitionData, source->source))
            {
                if (ShouldLogTextureSyncFailure(targetIndex))
                {
                    Log("Cannot sync live %s tex timeline: append source %d failed", partKind, sourceIndex);
                }

                LeaveCriticalSection(&g_textureAlignmentLock);
                return 0;
            }

            currentFrames = MovieClipDataFrameCount(definitionData);

            if (currentFrames != nextExpected)
            {
                if (ShouldLogTextureSyncFailure(targetIndex))
                {
                    Log("Cannot sync live %s tex timeline: append source %d produced extent %d, expected %d", partKind, sourceIndex, currentFrames, nextExpected);
                }

                LeaveCriticalSection(&g_textureAlignmentLock);
                return 0;
            }
            ++applied;
        }
        else if (currentFrames < nextExpected)
        {
            if (ShouldLogTextureSyncFailure(targetIndex))
            {
                Log("Cannot sync live %s tex timeline: frame count %d falls inside expected append boundary %d..%d", partKind, currentFrames, expectedFrames, nextExpected);
            }
            
            LeaveCriticalSection(&g_textureAlignmentLock);
            return 0;
        }

        expectedFrames = nextExpected;
    }

    if (currentFrames != expectedFrames)
    {
        if (ShouldLogTextureSyncFailure(targetIndex))
        {
            Log("Cannot sync live %s tex timeline: frame count %d does not match vanilla+MCPF extent %d", partKind, currentFrames, expectedFrames);
        }

        LeaveCriticalSection(&g_textureAlignmentLock);
        return 0;
    }

    InterlockedExchange(&g_textureSyncFailureLogged[targetIndex], 0);
    RememberTextureSyncCache(targetIndex, definitionData, sourceCount, currentFrames);
    LeaveCriticalSection(&g_textureAlignmentLock);
    
    if (applied > 0)
    {
        Log("Mirrored %d texture timeline append(s) into live %s tex definition; extent=%d", applied, partKind, currentFrames);
    }

    return 1;
}

static const char* TextureKindForPartState(void* partState, void* catParts)
{
    uintptr_t stateAddress;
    uintptr_t partsAddress;
    size_t stateOffset;

    if (!partState || !catParts)
    {
        return NULL;
    }

    stateAddress = (uintptr_t)partState;
    partsAddress = (uintptr_t)catParts;

    if (stateAddress < partsAddress)
    {
        return NULL;
    }

    stateOffset = (size_t)(stateAddress - partsAddress);

    switch (stateOffset)
    {
        case CATPART_BODY_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
            return "body";
        case CATPART_HEAD_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
            return "head";
        case CATPART_TAIL_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
            return "tail";
        case CATPART_LEG1_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
        case CATPART_LEG2_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
        case CATPART_ARM1_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
        case CATPART_ARM2_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
            return "leg";
        case CATPART_LEFTEAR_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
        case CATPART_RIGHTEAR_ID_OFFSET - CATPART_STATE_PREFIX_SIZE:
            return "ear";
        default:
            return NULL;
    }
}

static void SyncPreparedCatPartGraphicsTexture(void* graphics, void* partState, void* catParts)
{
    const char* kind;
    uint8_t* entries;
    int32_t entryTotal;
    int32_t oneBasedTexture;
    int32_t index;
    size_t byteCount;

    if (!graphics || !partState || !catParts || !g_setMovieClipFrame)
    {
        return;
    }

    kind = TextureKindForPartState(partState, catParts);

    if (!kind)
    {
        return;
    }

    if (!IsMemoryRangeAccessible((uint8_t*)partState + CATPART_STATE_TEXTURE_OFFSET, sizeof(oneBasedTexture), 0) || !IsMemoryRangeAccessible( graphics, CATPART_GRAPHICS_ENTRIES_OFFSET + sizeof(entries), 0))
    {
        return;
    }

    memcpy(&oneBasedTexture, (uint8_t*)partState + CATPART_STATE_TEXTURE_OFFSET, sizeof(oneBasedTexture));
    memcpy(&entryTotal, (uint8_t*)graphics + CATPART_GRAPHICS_ENTRY_TOTAL_OFFSET, sizeof(entryTotal));
    memcpy(&entries, (uint8_t*)graphics + CATPART_GRAPHICS_ENTRIES_OFFSET, sizeof(entries));

    if (oneBasedTexture < 1 || entryTotal <= 0 || entryTotal > 4096 || !entries || (size_t)entryTotal > SIZE_MAX / CATPART_GRAPHICS_ENTRY_SIZE)
    {
        return;
    }

    byteCount = (size_t)entryTotal * CATPART_GRAPHICS_ENTRY_SIZE;

    if (!IsMemoryRangeAccessible(entries, byteCount, 0))
    {
        return;
    }

    for (index = 0; index < entryTotal; ++index)
    {
        uint8_t* entry = entries + (size_t)index * CATPART_GRAPHICS_ENTRY_SIZE;
        void* textureClip = NULL;
        void* definitionData = NULL;
        int32_t frameExtent;

        /* 
        * The native CatPartGraphics routine has already resolved tex into
        * entry + 0x08. Synchronize that exact definition on first use. This
        * is earlier and more general than CatVisual::Refresh: constructors and
        * adventure-spawn crap calls this helper directly as well...
        */
        memcpy(&textureClip, entry + CATPART_GRAPHICS_TEXTURE_CLIP_OFFSET, sizeof(textureClip));
        
        if (!textureClip || !MewCatPartFramework_SyncTextureClip(kind, textureClip) || !IsMemoryRangeAccessible((uint8_t*)textureClip + LIVE_MOVIE_CLIP_DEFINITION_OFFSET, sizeof(definitionData), 0))
        {
            continue;
        }

        memcpy(&definitionData, (uint8_t*)textureClip + LIVE_MOVIE_CLIP_DEFINITION_OFFSET, sizeof(definitionData));
        frameExtent = MovieClipDataFrameCount(definitionData);
        
        if (oneBasedTexture <= frameExtent)
        {
            // Re-apply it immediately so the very first rendered instance is already correct...
            g_setMovieClipFrame(textureClip, oneBasedTexture - 1);
        }
    }
}

static void __fastcall HookCatPartGraphicsRefresh(void* graphics, void* partState, void* catParts, int32_t palette)
{
    if (g_origCatPartGraphicsRefresh)
    {
        g_origCatPartGraphicsRefresh(graphics, partState, catParts, palette);
    }

    SyncPreparedCatPartGraphicsTexture(graphics, partState, catParts);
}

static void MaybeResolveNamedPart(void* field, const char* expectedKind)
{
    char id[MAX_ID_LENGTH];
    int32_t frame;
    PartDefinition* part;

    if (!ReadNamedPartId(field, id, sizeof(id)))
    {
        return;
    }

    EnsureManifestsLoaded();
    part = FindPart(id);

    if (!part)
    {
        Log("Unknown named cat part: @%s", id);
        return;
    }

    if (strcmp(part->kind, expectedKind) != 0)
    {
        Log("@%s is a %s part, not valid for this %s field", id, part->kind, expectedKind);
        return;
    }

    if (!ResolvePartFrameAfterActiveBindings(part, &frame))
    {
        QueuePendingNamedField(field, id, expectedKind);
        return;
    }

    if (ApplyResolvedPartFrame(field, frame))
    {
        Log("Resolved @%s => frame %d", id, frame);
    }
}

static int FieldLooksLikeNamedPartFast(const void* field)
{
    const uint8_t* bytes = (const uint8_t*)field;
    const uint8_t* stringBytes;
    const char* text;
    size_t size;
    size_t capacity;
    int32_t type;

    if (!bytes)
    {
        return 0;
    }

    memcpy(&type, bytes + GON_TYPE_OFFSET, sizeof(type));

    if (type != GON_TYPE_STRING)
    {
        return 0;
    }

    stringBytes = bytes + GON_STRING_DATA_OFFSET;
    memcpy(&size, stringBytes + MSVC_STRING_SIZE_OFFSET, sizeof(size));
    memcpy(&capacity, stringBytes + MSVC_STRING_CAPACITY_OFFSET, sizeof(capacity));

    if (size < 2U || size > MAX_GON_STRING_LENGTH || capacity < size)
    {
        return 0;
    }

    if (capacity <= MSVC_STRING_SSO_CAPACITY)
    {
        text = (const char*)stringBytes;
    }
    else
    {
        memcpy(&text, stringBytes, sizeof(text));
    }

    return text && text[0] == '@';
}

static void* __fastcall HookGonIndexByNameConst(void* gonObject, const void* fieldName)
{
    void* field = g_origGonIndexByNameConst ? g_origGonIndexByNameConst(gonObject, fieldName) : NULL;
    const char* kind;

    if (!FieldLooksLikeNamedPartFast(field))
    {
        return field;
    }

    kind = KindForGonField(fieldName);

    if (kind)
    {
        MaybeResolveNamedPart(field, kind);
    }

    return field;
}

static void* __fastcall HookGonIndexByName(void* gonObject, const void* fieldName)
{
    void* field = g_origGonIndexByName ? g_origGonIndexByName(gonObject, fieldName) : NULL;
    const char* kind;

    if (!FieldLooksLikeNamedPartFast(field))
    {
        return field;
    }

    kind = KindForGonField(fieldName);

    if (kind)
    {
        MaybeResolveNamedPart(field, kind);
    }

    return field;
}

static int VerifyBytes(UINT_PTR base, UINT_PTR rva, const uint8_t* expected, size_t count)
{
    return memcmp((const void*)(base + rva), expected, count) == 0;
}

static int InstallHooks(void)
{
    static const uint8_t processMergesBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x20 };
    static const uint8_t findBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
    static const uint8_t findCharacterBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x20 };
    static const uint8_t appendBytes[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10 };
    static const uint8_t gonConstBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
    static const uint8_t catPartGraphicsRefreshBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x48, 0x89, 0x4C, 0x24, 0x08 };
    static const uint8_t setFrameBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 };
    LONG state;
    UINT_PTR base;
    void* trampoline = NULL;

    state = InterlockedCompareExchange(&g_hookInstallState, 1, 0);
    
    if (state != 0)
    {
        return state == 1 || state == 2;
    }

    if (!MJ_Resolve(&g_mj))
    {
        InterlockedExchange(&g_hookInstallState, 0);
        return 0;
    }

    base = g_mj.GetGameBase();

    if (!base)
    {
        Log("Failed to get base address!");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_findSwfCharacter = (fn_find_swf_character)(base + RVA_FIND_SWF_CHARACTER);
    g_setMovieClipFrame = (fn_movie_clip_set_frame)(base + RVA_MOVIE_CLIP_SET_FRAME);

    /*
    * Install the outer merge gate first. Otherwise the game could run
    * process_animation_merges after the find hook was installed but before
    * the append hook was installed (or before either hook), losing a batch
    * nondeterministically. Calls entering this gate wait until every
    * dependent hook and trampoline below is ready...
    */
    if (!g_mj.InstallHook(RVA_PROCESS_ANIMATION_MERGES, PROCESS_MERGES_HOOK_STOLEN_BYTES, (void*)HookProcessAnimationMerges, &trampoline, 5, MOD_NAME))
    {
        Log("Failed to install animation-merge startup gate");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_origProcessAnimationMerges = (fn_process_animation_merges)trampoline;
    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_FIND_SWF_EXPORT, FIND_EXPORT_HOOK_STOLEN_BYTES, (void*)HookFindSwfExport, &trampoline, 10, MOD_NAME))
    {
        Log("Failed to install annotated-export hook");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_origFindSwfExport = (fn_find_swf_export)trampoline;
    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_APPEND_MOVIE_CLIP, APPEND_CLIP_HOOK_STOLEN_BYTES, (void*)HookAppendMovieClip, &trampoline, 10, MOD_NAME))
    {
        Log("Failed to install MovieClip append hook");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_origAppendMovieClip = (fn_append_movie_clip)trampoline;
    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_CAT_PART_GRAPHICS_REFRESH, CAT_PART_GRAPHICS_REFRESH_HOOK_STOLEN_BYTES, (void*)HookCatPartGraphicsRefresh, &trampoline, 20, MOD_NAME))
    {
        Log("Failed to install first-use CatPartGraphics texture synchronization hook");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_origCatPartGraphicsRefresh = (fn_cat_part_graphics_refresh)trampoline;
    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_GON_INDEX_BY_NAME_CONST, GON_INDEX_HOOK_STOLEN_BYTES, (void*)HookGonIndexByNameConst, &trampoline, 20, MOD_NAME))
    {
        Log("Failed to install const GON hook");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_origGonIndexByNameConst = (fn_gon_index_by_name)trampoline;
    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_GON_INDEX_BY_NAME, GON_INDEX_HOOK_STOLEN_BYTES, (void*)HookGonIndexByName, &trampoline, 20, MOD_NAME))
    {
        Log("Failed to install mutable GON hook");
        InterlockedExchange(&g_hookInstallState, 3);
        return 0;
    }

    g_origGonIndexByName = (fn_gon_index_by_name)trampoline;
    MemoryBarrier();
    InterlockedExchange(&g_hookInstallState, 2);
    Log("Installed!");
    return 1;
}

__declspec(dllexport) void __cdecl MewCatPartFramework_Init(void)
{
    InstallHooks();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_moduleHandle = module;
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_registryLock);
        InitializeCriticalSection(&g_pendingFieldLock);
        InitializeCriticalSection(&g_textureAlignmentLock);
        MJ_Resolve(&g_mj);
        InstallHooks();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        DeleteCriticalSection(&g_textureAlignmentLock);
        DeleteCriticalSection(&g_pendingFieldLock);
        DeleteCriticalSection(&g_registryLock);
    }

    return TRUE;
}