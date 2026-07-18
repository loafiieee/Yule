#include <windows.h>
extern HMODULE real_sdl;
unsigned int __cdecl luna_SDL_GetTicks(void);
unsigned long long __cdecl luna_SDL_GetPerformanceCounter(void);
unsigned long long __cdecl luna_SDL_GetPerformanceFrequency(void);

void* p_SDL_AddEventWatch = NULL;
void* p_SDL_AddHintCallback = NULL;
void* p_SDL_AddTimer = NULL;
void* p_SDL_AllocFormat = NULL;
void* p_SDL_AllocPalette = NULL;
void* p_SDL_AllocRW = NULL;
void* p_SDL_AtomicAdd = NULL;
void* p_SDL_AtomicCAS = NULL;
void* p_SDL_AtomicCASPtr = NULL;
void* p_SDL_AtomicGet = NULL;
void* p_SDL_AtomicGetPtr = NULL;
void* p_SDL_AtomicLock = NULL;
void* p_SDL_AtomicSet = NULL;
void* p_SDL_AtomicSetPtr = NULL;
void* p_SDL_AtomicTryLock = NULL;
void* p_SDL_AtomicUnlock = NULL;
void* p_SDL_AudioInit = NULL;
void* p_SDL_AudioQuit = NULL;
void* p_SDL_BuildAudioCVT = NULL;
void* p_SDL_CalculateGammaRamp = NULL;
void* p_SDL_ClearError = NULL;
void* p_SDL_ClearHints = NULL;
void* p_SDL_CloseAudio = NULL;
void* p_SDL_CloseAudioDevice = NULL;
void* p_SDL_CondBroadcast = NULL;
void* p_SDL_CondSignal = NULL;
void* p_SDL_CondWait = NULL;
void* p_SDL_CondWaitTimeout = NULL;
void* p_SDL_ConvertAudio = NULL;
void* p_SDL_ConvertPixels = NULL;
void* p_SDL_ConvertSurface = NULL;
void* p_SDL_ConvertSurfaceFormat = NULL;
void* p_SDL_CreateColorCursor = NULL;
void* p_SDL_CreateCond = NULL;
void* p_SDL_CreateCursor = NULL;
void* p_SDL_CreateMutex = NULL;
void* p_SDL_CreateRGBSurface = NULL;
void* p_SDL_CreateRGBSurfaceFrom = NULL;
void* p_SDL_CreateRenderer = NULL;
void* p_SDL_CreateSemaphore = NULL;
void* p_SDL_CreateShapedWindow = NULL;
void* p_SDL_CreateSoftwareRenderer = NULL;
void* p_SDL_CreateSystemCursor = NULL;
void* p_SDL_CreateTexture = NULL;
void* p_SDL_CreateTextureFromSurface = NULL;
void* p_SDL_CreateThread = NULL;
void* p_SDL_CreateWindow = NULL;
void* p_SDL_CreateWindowAndRenderer = NULL;
void* p_SDL_CreateWindowFrom = NULL;
void* p_SDL_DXGIGetOutputInfo = NULL;
void* p_SDL_DYNAPI_entry = NULL;
void* p_SDL_DelEventWatch = NULL;
void* p_SDL_DelHintCallback = NULL;
void* p_SDL_Delay = NULL;
void* p_SDL_DestroyCond = NULL;
void* p_SDL_DestroyMutex = NULL;
void* p_SDL_DestroyRenderer = NULL;
void* p_SDL_DestroySemaphore = NULL;
void* p_SDL_DestroyTexture = NULL;
void* p_SDL_DestroyWindow = NULL;
void* p_SDL_DetachThread = NULL;
void* p_SDL_Direct3D9GetAdapterIndex = NULL;
void* p_SDL_DisableScreenSaver = NULL;
void* p_SDL_EnableScreenSaver = NULL;
void* p_SDL_EnclosePoints = NULL;
void* p_SDL_Error = NULL;
void* p_SDL_EventState = NULL;
void* p_SDL_FillRect = NULL;
void* p_SDL_FillRects = NULL;
void* p_SDL_FilterEvents = NULL;
void* p_SDL_FlushEvent = NULL;
void* p_SDL_FlushEvents = NULL;
void* p_SDL_FreeCursor = NULL;
void* p_SDL_FreeFormat = NULL;
void* p_SDL_FreePalette = NULL;
void* p_SDL_FreeRW = NULL;
void* p_SDL_FreeSurface = NULL;
void* p_SDL_FreeWAV = NULL;
void* p_SDL_GL_BindTexture = NULL;
void* p_SDL_GL_CreateContext = NULL;
void* p_SDL_GL_DeleteContext = NULL;
void* p_SDL_GL_ExtensionSupported = NULL;
void* p_SDL_GL_GetAttribute = NULL;
void* p_SDL_GL_GetCurrentContext = NULL;
void* p_SDL_GL_GetCurrentWindow = NULL;
void* p_SDL_GL_GetDrawableSize = NULL;
void* p_SDL_GL_GetProcAddress = NULL;
void* p_SDL_GL_GetSwapInterval = NULL;
void* p_SDL_GL_LoadLibrary = NULL;
void* p_SDL_GL_MakeCurrent = NULL;
void* p_SDL_GL_ResetAttributes = NULL;
void* p_SDL_GL_SetAttribute = NULL;
void* p_SDL_GL_SetSwapInterval = NULL;
void* p_SDL_GL_UnbindTexture = NULL;
void* p_SDL_GL_UnloadLibrary = NULL;
void* p_SDL_GameControllerAddMapping = NULL;
void* p_SDL_GameControllerAddMappingsFromRW = NULL;
void* p_SDL_GameControllerClose = NULL;
void* p_SDL_GameControllerEventState = NULL;
void* p_SDL_GameControllerGetAttached = NULL;
void* p_SDL_GameControllerGetAxis = NULL;
void* p_SDL_GameControllerGetAxisFromString = NULL;
void* p_SDL_GameControllerGetBindForAxis = NULL;
void* p_SDL_GameControllerGetBindForButton = NULL;
void* p_SDL_GameControllerGetButton = NULL;
void* p_SDL_GameControllerGetButtonFromString = NULL;
void* p_SDL_GameControllerGetJoystick = NULL;
void* p_SDL_GameControllerGetStringForAxis = NULL;
void* p_SDL_GameControllerGetStringForButton = NULL;
void* p_SDL_GameControllerMapping = NULL;
void* p_SDL_GameControllerMappingForGUID = NULL;
void* p_SDL_GameControllerName = NULL;
void* p_SDL_GameControllerNameForIndex = NULL;
void* p_SDL_GameControllerOpen = NULL;
void* p_SDL_GameControllerUpdate = NULL;
void* p_SDL_GetAssertionHandler = NULL;
void* p_SDL_GetAssertionReport = NULL;
void* p_SDL_GetAudioDeviceName = NULL;
void* p_SDL_GetAudioDeviceStatus = NULL;
void* p_SDL_GetAudioDriver = NULL;
void* p_SDL_GetAudioStatus = NULL;
void* p_SDL_GetBasePath = NULL;
void* p_SDL_GetCPUCacheLineSize = NULL;
void* p_SDL_GetCPUCount = NULL;
void* p_SDL_GetClipRect = NULL;
void* p_SDL_GetClipboardText = NULL;
void* p_SDL_GetClosestDisplayMode = NULL;
void* p_SDL_GetColorKey = NULL;
void* p_SDL_GetCurrentAudioDriver = NULL;
void* p_SDL_GetCurrentDisplayMode = NULL;
void* p_SDL_GetCurrentVideoDriver = NULL;
void* p_SDL_GetCursor = NULL;
void* p_SDL_GetDefaultAssertionHandler = NULL;
void* p_SDL_GetDefaultCursor = NULL;
void* p_SDL_GetDesktopDisplayMode = NULL;
void* p_SDL_GetDisplayBounds = NULL;
void* p_SDL_GetDisplayMode = NULL;
void* p_SDL_GetDisplayName = NULL;
void* p_SDL_GetError = NULL;
void* p_SDL_GetEventFilter = NULL;
void* p_SDL_GetHint = NULL;
void* p_SDL_GetKeyFromName = NULL;
void* p_SDL_GetKeyFromScancode = NULL;
void* p_SDL_GetKeyName = NULL;
void* p_SDL_GetKeyboardFocus = NULL;
void* p_SDL_GetKeyboardState = NULL;
void* p_SDL_GetModState = NULL;
void* p_SDL_GetMouseFocus = NULL;
void* p_SDL_GetMouseState = NULL;
void* p_SDL_GetNumAudioDevices = NULL;
void* p_SDL_GetNumAudioDrivers = NULL;
void* p_SDL_GetNumDisplayModes = NULL;
void* p_SDL_GetNumRenderDrivers = NULL;
void* p_SDL_GetNumTouchDevices = NULL;
void* p_SDL_GetNumTouchFingers = NULL;
void* p_SDL_GetNumVideoDisplays = NULL;
void* p_SDL_GetNumVideoDrivers = NULL;
void* p_SDL_GetPerformanceCounter = NULL;
void* p_SDL_GetPerformanceFrequency = NULL;
void* p_SDL_GetPixelFormatName = NULL;
void* p_SDL_GetPlatform = NULL;
void* p_SDL_GetPowerInfo = NULL;
void* p_SDL_GetPrefPath = NULL;
void* p_SDL_GetRGB = NULL;
void* p_SDL_GetRGBA = NULL;
void* p_SDL_GetRelativeMouseMode = NULL;
void* p_SDL_GetRelativeMouseState = NULL;
void* p_SDL_GetRenderDrawBlendMode = NULL;
void* p_SDL_GetRenderDrawColor = NULL;
void* p_SDL_GetRenderDriverInfo = NULL;
void* p_SDL_GetRenderTarget = NULL;
void* p_SDL_GetRenderer = NULL;
void* p_SDL_GetRendererInfo = NULL;
void* p_SDL_GetRendererOutputSize = NULL;
void* p_SDL_GetRevision = NULL;
void* p_SDL_GetRevisionNumber = NULL;
void* p_SDL_GetScancodeFromKey = NULL;
void* p_SDL_GetScancodeFromName = NULL;
void* p_SDL_GetScancodeName = NULL;
void* p_SDL_GetShapedWindowMode = NULL;
void* p_SDL_GetSurfaceAlphaMod = NULL;
void* p_SDL_GetSurfaceBlendMode = NULL;
void* p_SDL_GetSurfaceColorMod = NULL;
void* p_SDL_GetSystemRAM = NULL;
void* p_SDL_GetTextureAlphaMod = NULL;
void* p_SDL_GetTextureBlendMode = NULL;
void* p_SDL_GetTextureColorMod = NULL;
void* p_SDL_GetThreadID = NULL;
void* p_SDL_GetThreadName = NULL;
void* p_SDL_GetTicks = NULL;
void* p_SDL_GetTouchDevice = NULL;
void* p_SDL_GetTouchFinger = NULL;
void* p_SDL_GetVersion = NULL;
void* p_SDL_GetVideoDriver = NULL;
void* p_SDL_GetWindowBrightness = NULL;
void* p_SDL_GetWindowData = NULL;
void* p_SDL_GetWindowDisplayIndex = NULL;
void* p_SDL_GetWindowDisplayMode = NULL;
void* p_SDL_GetWindowFlags = NULL;
void* p_SDL_GetWindowFromID = NULL;
void* p_SDL_GetWindowGammaRamp = NULL;
void* p_SDL_GetWindowGrab = NULL;
void* p_SDL_GetWindowID = NULL;
void* p_SDL_GetWindowMaximumSize = NULL;
void* p_SDL_GetWindowMinimumSize = NULL;
void* p_SDL_GetWindowPixelFormat = NULL;
void* p_SDL_GetWindowPosition = NULL;
void* p_SDL_GetWindowSize = NULL;
void* p_SDL_GetWindowSurface = NULL;
void* p_SDL_GetWindowTitle = NULL;
void* p_SDL_GetWindowWMInfo = NULL;
void* p_SDL_HapticClose = NULL;
void* p_SDL_HapticDestroyEffect = NULL;
void* p_SDL_HapticEffectSupported = NULL;
void* p_SDL_HapticGetEffectStatus = NULL;
void* p_SDL_HapticIndex = NULL;
void* p_SDL_HapticName = NULL;
void* p_SDL_HapticNewEffect = NULL;
void* p_SDL_HapticNumAxes = NULL;
void* p_SDL_HapticNumEffects = NULL;
void* p_SDL_HapticNumEffectsPlaying = NULL;
void* p_SDL_HapticOpen = NULL;
void* p_SDL_HapticOpenFromJoystick = NULL;
void* p_SDL_HapticOpenFromMouse = NULL;
void* p_SDL_HapticOpened = NULL;
void* p_SDL_HapticPause = NULL;
void* p_SDL_HapticQuery = NULL;
void* p_SDL_HapticRumbleInit = NULL;
void* p_SDL_HapticRumblePlay = NULL;
void* p_SDL_HapticRumbleStop = NULL;
void* p_SDL_HapticRumbleSupported = NULL;
void* p_SDL_HapticRunEffect = NULL;
void* p_SDL_HapticSetAutocenter = NULL;
void* p_SDL_HapticSetGain = NULL;
void* p_SDL_HapticStopAll = NULL;
void* p_SDL_HapticStopEffect = NULL;
void* p_SDL_HapticUnpause = NULL;
void* p_SDL_HapticUpdateEffect = NULL;
void* p_SDL_Has3DNow = NULL;
void* p_SDL_HasAVX = NULL;
void* p_SDL_HasAltiVec = NULL;
void* p_SDL_HasClipboardText = NULL;
void* p_SDL_HasEvent = NULL;
void* p_SDL_HasEvents = NULL;
void* p_SDL_HasIntersection = NULL;
void* p_SDL_HasMMX = NULL;
void* p_SDL_HasRDTSC = NULL;
void* p_SDL_HasSSE = NULL;
void* p_SDL_HasSSE2 = NULL;
void* p_SDL_HasSSE3 = NULL;
void* p_SDL_HasSSE41 = NULL;
void* p_SDL_HasSSE42 = NULL;
void* p_SDL_HasScreenKeyboardSupport = NULL;
void* p_SDL_HideWindow = NULL;
void* p_SDL_Init = NULL;
void* p_SDL_InitSubSystem = NULL;
void* p_SDL_IntersectRect = NULL;
void* p_SDL_IntersectRectAndLine = NULL;
void* p_SDL_IsGameController = NULL;
void* p_SDL_IsScreenKeyboardShown = NULL;
void* p_SDL_IsScreenSaverEnabled = NULL;
void* p_SDL_IsShapedWindow = NULL;
void* p_SDL_IsTextInputActive = NULL;
void* p_SDL_JoystickClose = NULL;
void* p_SDL_JoystickEventState = NULL;
void* p_SDL_JoystickGetAttached = NULL;
void* p_SDL_JoystickGetAxis = NULL;
void* p_SDL_JoystickGetBall = NULL;
void* p_SDL_JoystickGetButton = NULL;
void* p_SDL_JoystickGetDeviceGUID = NULL;
void* p_SDL_JoystickGetGUID = NULL;
void* p_SDL_JoystickGetGUIDFromString = NULL;
void* p_SDL_JoystickGetGUIDString = NULL;
void* p_SDL_JoystickGetHat = NULL;
void* p_SDL_JoystickInstanceID = NULL;
void* p_SDL_JoystickIsHaptic = NULL;
void* p_SDL_JoystickName = NULL;
void* p_SDL_JoystickNameForIndex = NULL;
void* p_SDL_JoystickNumAxes = NULL;
void* p_SDL_JoystickNumBalls = NULL;
void* p_SDL_JoystickNumButtons = NULL;
void* p_SDL_JoystickNumHats = NULL;
void* p_SDL_JoystickOpen = NULL;
void* p_SDL_JoystickUpdate = NULL;
void* p_SDL_LoadBMP_RW = NULL;
void* p_SDL_LoadDollarTemplates = NULL;
void* p_SDL_LoadFunction = NULL;
void* p_SDL_LoadObject = NULL;
void* p_SDL_LoadWAV_RW = NULL;
void* p_SDL_LockAudio = NULL;
void* p_SDL_LockAudioDevice = NULL;
void* p_SDL_LockMutex = NULL;
void* p_SDL_LockSurface = NULL;
void* p_SDL_LockTexture = NULL;
void* p_SDL_Log = NULL;
void* p_SDL_LogCritical = NULL;
void* p_SDL_LogDebug = NULL;
void* p_SDL_LogError = NULL;
void* p_SDL_LogGetOutputFunction = NULL;
void* p_SDL_LogGetPriority = NULL;
void* p_SDL_LogInfo = NULL;
void* p_SDL_LogMessage = NULL;
void* p_SDL_LogMessageV = NULL;
void* p_SDL_LogResetPriorities = NULL;
void* p_SDL_LogSetAllPriority = NULL;
void* p_SDL_LogSetOutputFunction = NULL;
void* p_SDL_LogSetPriority = NULL;
void* p_SDL_LogVerbose = NULL;
void* p_SDL_LogWarn = NULL;
void* p_SDL_LowerBlit = NULL;
void* p_SDL_LowerBlitScaled = NULL;
void* p_SDL_MapRGB = NULL;
void* p_SDL_MapRGBA = NULL;
void* p_SDL_MasksToPixelFormatEnum = NULL;
void* p_SDL_MaximizeWindow = NULL;
void* p_SDL_MinimizeWindow = NULL;
void* p_SDL_MixAudio = NULL;
void* p_SDL_MixAudioFormat = NULL;
void* p_SDL_MouseIsHaptic = NULL;
void* p_SDL_NumHaptics = NULL;
void* p_SDL_NumJoysticks = NULL;
void* p_SDL_OpenAudio = NULL;
void* p_SDL_OpenAudioDevice = NULL;
void* p_SDL_PauseAudio = NULL;
void* p_SDL_PauseAudioDevice = NULL;
void* p_SDL_PeepEvents = NULL;
void* p_SDL_PixelFormatEnumToMasks = NULL;
void* p_SDL_PumpEvents = NULL;
void* p_SDL_PushEvent = NULL;
void* p_SDL_QueryTexture = NULL;
void* p_SDL_Quit = NULL;
void* p_SDL_QuitSubSystem = NULL;
void* p_SDL_RWFromConstMem = NULL;
void* p_SDL_RWFromFP = NULL;
void* p_SDL_RWFromFile = NULL;
void* p_SDL_RWFromMem = NULL;
void* p_SDL_RaiseWindow = NULL;
void* p_SDL_ReadBE16 = NULL;
void* p_SDL_ReadBE32 = NULL;
void* p_SDL_ReadBE64 = NULL;
void* p_SDL_ReadLE16 = NULL;
void* p_SDL_ReadLE32 = NULL;
void* p_SDL_ReadLE64 = NULL;
void* p_SDL_ReadU8 = NULL;
void* p_SDL_RecordGesture = NULL;
void* p_SDL_RegisterApp = NULL;
void* p_SDL_RegisterEvents = NULL;
void* p_SDL_RemoveTimer = NULL;
void* p_SDL_RenderClear = NULL;
void* p_SDL_RenderCopy = NULL;
void* p_SDL_RenderCopyEx = NULL;
void* p_SDL_RenderDrawLine = NULL;
void* p_SDL_RenderDrawLines = NULL;
void* p_SDL_RenderDrawPoint = NULL;
void* p_SDL_RenderDrawPoints = NULL;
void* p_SDL_RenderDrawRect = NULL;
void* p_SDL_RenderDrawRects = NULL;
void* p_SDL_RenderFillRect = NULL;
void* p_SDL_RenderFillRects = NULL;
void* p_SDL_RenderGetClipRect = NULL;
void* p_SDL_RenderGetD3D9Device = NULL;
void* p_SDL_RenderGetLogicalSize = NULL;
void* p_SDL_RenderGetScale = NULL;
void* p_SDL_RenderGetViewport = NULL;
void* p_SDL_RenderPresent = NULL;
void* p_SDL_RenderReadPixels = NULL;
void* p_SDL_RenderSetClipRect = NULL;
void* p_SDL_RenderSetLogicalSize = NULL;
void* p_SDL_RenderSetScale = NULL;
void* p_SDL_RenderSetViewport = NULL;
void* p_SDL_RenderTargetSupported = NULL;
void* p_SDL_ReportAssertion = NULL;
void* p_SDL_ResetAssertionReport = NULL;
void* p_SDL_RestoreWindow = NULL;
void* p_SDL_SaveAllDollarTemplates = NULL;
void* p_SDL_SaveBMP_RW = NULL;
void* p_SDL_SaveDollarTemplate = NULL;
void* p_SDL_SemPost = NULL;
void* p_SDL_SemTryWait = NULL;
void* p_SDL_SemValue = NULL;
void* p_SDL_SemWait = NULL;
void* p_SDL_SemWaitTimeout = NULL;
void* p_SDL_SetAssertionHandler = NULL;
void* p_SDL_SetClipRect = NULL;
void* p_SDL_SetClipboardText = NULL;
void* p_SDL_SetColorKey = NULL;
void* p_SDL_SetCursor = NULL;
void* p_SDL_SetError = NULL;
void* p_SDL_SetEventFilter = NULL;
void* p_SDL_SetHint = NULL;
void* p_SDL_SetHintWithPriority = NULL;
void* p_SDL_SetMainReady = NULL;
void* p_SDL_SetModState = NULL;
void* p_SDL_SetPaletteColors = NULL;
void* p_SDL_SetPixelFormatPalette = NULL;
void* p_SDL_SetRelativeMouseMode = NULL;
void* p_SDL_SetRenderDrawBlendMode = NULL;
void* p_SDL_SetRenderDrawColor = NULL;
void* p_SDL_SetRenderTarget = NULL;
void* p_SDL_SetSurfaceAlphaMod = NULL;
void* p_SDL_SetSurfaceBlendMode = NULL;
void* p_SDL_SetSurfaceColorMod = NULL;
void* p_SDL_SetSurfacePalette = NULL;
void* p_SDL_SetSurfaceRLE = NULL;
void* p_SDL_SetTextInputRect = NULL;
void* p_SDL_SetTextureAlphaMod = NULL;
void* p_SDL_SetTextureBlendMode = NULL;
void* p_SDL_SetTextureColorMod = NULL;
void* p_SDL_SetThreadPriority = NULL;
void* p_SDL_SetWindowBordered = NULL;
void* p_SDL_SetWindowBrightness = NULL;
void* p_SDL_SetWindowData = NULL;
void* p_SDL_SetWindowDisplayMode = NULL;
void* p_SDL_SetWindowFullscreen = NULL;
void* p_SDL_SetWindowGammaRamp = NULL;
void* p_SDL_SetWindowGrab = NULL;
void* p_SDL_SetWindowIcon = NULL;
void* p_SDL_SetWindowMaximumSize = NULL;
void* p_SDL_SetWindowMinimumSize = NULL;
void* p_SDL_SetWindowPosition = NULL;
void* p_SDL_SetWindowShape = NULL;
void* p_SDL_SetWindowSize = NULL;
void* p_SDL_SetWindowTitle = NULL;
void* p_SDL_ShowCursor = NULL;
void* p_SDL_ShowMessageBox = NULL;
void* p_SDL_ShowSimpleMessageBox = NULL;
void* p_SDL_ShowWindow = NULL;
void* p_SDL_SoftStretch = NULL;
void* p_SDL_StartTextInput = NULL;
void* p_SDL_StopTextInput = NULL;
void* p_SDL_TLSCreate = NULL;
void* p_SDL_TLSGet = NULL;
void* p_SDL_TLSSet = NULL;
void* p_SDL_ThreadID = NULL;
void* p_SDL_TryLockMutex = NULL;
void* p_SDL_UnionRect = NULL;
void* p_SDL_UnloadObject = NULL;
void* p_SDL_UnlockAudio = NULL;
void* p_SDL_UnlockAudioDevice = NULL;
void* p_SDL_UnlockMutex = NULL;
void* p_SDL_UnlockSurface = NULL;
void* p_SDL_UnlockTexture = NULL;
void* p_SDL_UnregisterApp = NULL;
void* p_SDL_UpdateTexture = NULL;
void* p_SDL_UpdateWindowSurface = NULL;
void* p_SDL_UpdateWindowSurfaceRects = NULL;
void* p_SDL_UpdateYUVTexture = NULL;
void* p_SDL_UpperBlit = NULL;
void* p_SDL_UpperBlitScaled = NULL;
void* p_SDL_VideoInit = NULL;
void* p_SDL_VideoQuit = NULL;
void* p_SDL_WaitEvent = NULL;
void* p_SDL_WaitEventTimeout = NULL;
void* p_SDL_WaitThread = NULL;
void* p_SDL_WarpMouseInWindow = NULL;
void* p_SDL_WasInit = NULL;
void* p_SDL_WriteBE16 = NULL;
void* p_SDL_WriteBE32 = NULL;
void* p_SDL_WriteBE64 = NULL;
void* p_SDL_WriteLE16 = NULL;
void* p_SDL_WriteLE32 = NULL;
void* p_SDL_WriteLE64 = NULL;
void* p_SDL_WriteU8 = NULL;
void* p_SDL_abs = NULL;
void* p_SDL_acos = NULL;
void* p_SDL_asin = NULL;
void* p_SDL_atan = NULL;
void* p_SDL_atan2 = NULL;
void* p_SDL_atof = NULL;
void* p_SDL_atoi = NULL;
void* p_SDL_calloc = NULL;
void* p_SDL_ceil = NULL;
void* p_SDL_copysign = NULL;
void* p_SDL_cos = NULL;
void* p_SDL_cosf = NULL;
void* p_SDL_fabs = NULL;
void* p_SDL_floor = NULL;
void* p_SDL_free = NULL;
void* p_SDL_getenv = NULL;
void* p_SDL_iconv = NULL;
void* p_SDL_iconv_close = NULL;
void* p_SDL_iconv_open = NULL;
void* p_SDL_iconv_string = NULL;
void* p_SDL_isdigit = NULL;
void* p_SDL_isspace = NULL;
void* p_SDL_itoa = NULL;
void* p_SDL_lltoa = NULL;
void* p_SDL_log = NULL;
void* p_SDL_ltoa = NULL;
void* p_SDL_malloc = NULL;
void* p_SDL_memcmp = NULL;
void* p_SDL_memcpy = NULL;
void* p_SDL_memmove = NULL;
void* p_SDL_memset = NULL;
void* p_SDL_pow = NULL;
void* p_SDL_qsort = NULL;
void* p_SDL_realloc = NULL;
void* p_SDL_scalbn = NULL;
void* p_SDL_setenv = NULL;
void* p_SDL_sin = NULL;
void* p_SDL_sinf = NULL;
void* p_SDL_snprintf = NULL;
void* p_SDL_sqrt = NULL;
void* p_SDL_sscanf = NULL;
void* p_SDL_strcasecmp = NULL;
void* p_SDL_strchr = NULL;
void* p_SDL_strcmp = NULL;
void* p_SDL_strdup = NULL;
void* p_SDL_strlcat = NULL;
void* p_SDL_strlcpy = NULL;
void* p_SDL_strlen = NULL;
void* p_SDL_strlwr = NULL;
void* p_SDL_strncasecmp = NULL;
void* p_SDL_strncmp = NULL;
void* p_SDL_strrchr = NULL;
void* p_SDL_strrev = NULL;
void* p_SDL_strstr = NULL;
void* p_SDL_strtod = NULL;
void* p_SDL_strtol = NULL;
void* p_SDL_strtoll = NULL;
void* p_SDL_strtoul = NULL;
void* p_SDL_strtoull = NULL;
void* p_SDL_strupr = NULL;
void* p_SDL_tolower = NULL;
void* p_SDL_toupper = NULL;
void* p_SDL_uitoa = NULL;
void* p_SDL_ulltoa = NULL;
void* p_SDL_ultoa = NULL;
void* p_SDL_utf8strlcpy = NULL;
void* p_SDL_vsnprintf = NULL;
void* p_SDL_vsscanf = NULL;
void* p_SDL_wcslcat = NULL;
void* p_SDL_wcslcpy = NULL;
void* p_SDL_wcslen = NULL;

void init_stubs() {
    p_SDL_AddEventWatch = GetProcAddress(real_sdl, "SDL_AddEventWatch");
    p_SDL_AddHintCallback = GetProcAddress(real_sdl, "SDL_AddHintCallback");
    p_SDL_AddTimer = GetProcAddress(real_sdl, "SDL_AddTimer");
    p_SDL_AllocFormat = GetProcAddress(real_sdl, "SDL_AllocFormat");
    p_SDL_AllocPalette = GetProcAddress(real_sdl, "SDL_AllocPalette");
    p_SDL_AllocRW = GetProcAddress(real_sdl, "SDL_AllocRW");
    p_SDL_AtomicAdd = GetProcAddress(real_sdl, "SDL_AtomicAdd");
    p_SDL_AtomicCAS = GetProcAddress(real_sdl, "SDL_AtomicCAS");
    p_SDL_AtomicCASPtr = GetProcAddress(real_sdl, "SDL_AtomicCASPtr");
    p_SDL_AtomicGet = GetProcAddress(real_sdl, "SDL_AtomicGet");
    p_SDL_AtomicGetPtr = GetProcAddress(real_sdl, "SDL_AtomicGetPtr");
    p_SDL_AtomicLock = GetProcAddress(real_sdl, "SDL_AtomicLock");
    p_SDL_AtomicSet = GetProcAddress(real_sdl, "SDL_AtomicSet");
    p_SDL_AtomicSetPtr = GetProcAddress(real_sdl, "SDL_AtomicSetPtr");
    p_SDL_AtomicTryLock = GetProcAddress(real_sdl, "SDL_AtomicTryLock");
    p_SDL_AtomicUnlock = GetProcAddress(real_sdl, "SDL_AtomicUnlock");
    p_SDL_AudioInit = GetProcAddress(real_sdl, "SDL_AudioInit");
    p_SDL_AudioQuit = GetProcAddress(real_sdl, "SDL_AudioQuit");
    p_SDL_BuildAudioCVT = GetProcAddress(real_sdl, "SDL_BuildAudioCVT");
    p_SDL_CalculateGammaRamp = GetProcAddress(real_sdl, "SDL_CalculateGammaRamp");
    p_SDL_ClearError = GetProcAddress(real_sdl, "SDL_ClearError");
    p_SDL_ClearHints = GetProcAddress(real_sdl, "SDL_ClearHints");
    p_SDL_CloseAudio = GetProcAddress(real_sdl, "SDL_CloseAudio");
    p_SDL_CloseAudioDevice = GetProcAddress(real_sdl, "SDL_CloseAudioDevice");
    p_SDL_CondBroadcast = GetProcAddress(real_sdl, "SDL_CondBroadcast");
    p_SDL_CondSignal = GetProcAddress(real_sdl, "SDL_CondSignal");
    p_SDL_CondWait = GetProcAddress(real_sdl, "SDL_CondWait");
    p_SDL_CondWaitTimeout = GetProcAddress(real_sdl, "SDL_CondWaitTimeout");
    p_SDL_ConvertAudio = GetProcAddress(real_sdl, "SDL_ConvertAudio");
    p_SDL_ConvertPixels = GetProcAddress(real_sdl, "SDL_ConvertPixels");
    p_SDL_ConvertSurface = GetProcAddress(real_sdl, "SDL_ConvertSurface");
    p_SDL_ConvertSurfaceFormat = GetProcAddress(real_sdl, "SDL_ConvertSurfaceFormat");
    p_SDL_CreateColorCursor = GetProcAddress(real_sdl, "SDL_CreateColorCursor");
    p_SDL_CreateCond = GetProcAddress(real_sdl, "SDL_CreateCond");
    p_SDL_CreateCursor = GetProcAddress(real_sdl, "SDL_CreateCursor");
    p_SDL_CreateMutex = GetProcAddress(real_sdl, "SDL_CreateMutex");
    p_SDL_CreateRGBSurface = GetProcAddress(real_sdl, "SDL_CreateRGBSurface");
    p_SDL_CreateRGBSurfaceFrom = GetProcAddress(real_sdl, "SDL_CreateRGBSurfaceFrom");
    p_SDL_CreateRenderer = GetProcAddress(real_sdl, "SDL_CreateRenderer");
    p_SDL_CreateSemaphore = GetProcAddress(real_sdl, "SDL_CreateSemaphore");
    p_SDL_CreateShapedWindow = GetProcAddress(real_sdl, "SDL_CreateShapedWindow");
    p_SDL_CreateSoftwareRenderer = GetProcAddress(real_sdl, "SDL_CreateSoftwareRenderer");
    p_SDL_CreateSystemCursor = GetProcAddress(real_sdl, "SDL_CreateSystemCursor");
    p_SDL_CreateTexture = GetProcAddress(real_sdl, "SDL_CreateTexture");
    p_SDL_CreateTextureFromSurface = GetProcAddress(real_sdl, "SDL_CreateTextureFromSurface");
    p_SDL_CreateThread = GetProcAddress(real_sdl, "SDL_CreateThread");
    p_SDL_CreateWindow = GetProcAddress(real_sdl, "SDL_CreateWindow");
    p_SDL_CreateWindowAndRenderer = GetProcAddress(real_sdl, "SDL_CreateWindowAndRenderer");
    p_SDL_CreateWindowFrom = GetProcAddress(real_sdl, "SDL_CreateWindowFrom");
    p_SDL_DXGIGetOutputInfo = GetProcAddress(real_sdl, "SDL_DXGIGetOutputInfo");
    p_SDL_DYNAPI_entry = GetProcAddress(real_sdl, "SDL_DYNAPI_entry");
    p_SDL_DelEventWatch = GetProcAddress(real_sdl, "SDL_DelEventWatch");
    p_SDL_DelHintCallback = GetProcAddress(real_sdl, "SDL_DelHintCallback");
    p_SDL_Delay = GetProcAddress(real_sdl, "SDL_Delay");
    p_SDL_DestroyCond = GetProcAddress(real_sdl, "SDL_DestroyCond");
    p_SDL_DestroyMutex = GetProcAddress(real_sdl, "SDL_DestroyMutex");
    p_SDL_DestroyRenderer = GetProcAddress(real_sdl, "SDL_DestroyRenderer");
    p_SDL_DestroySemaphore = GetProcAddress(real_sdl, "SDL_DestroySemaphore");
    p_SDL_DestroyTexture = GetProcAddress(real_sdl, "SDL_DestroyTexture");
    p_SDL_DestroyWindow = GetProcAddress(real_sdl, "SDL_DestroyWindow");
    p_SDL_DetachThread = GetProcAddress(real_sdl, "SDL_DetachThread");
    p_SDL_Direct3D9GetAdapterIndex = GetProcAddress(real_sdl, "SDL_Direct3D9GetAdapterIndex");
    p_SDL_DisableScreenSaver = GetProcAddress(real_sdl, "SDL_DisableScreenSaver");
    p_SDL_EnableScreenSaver = GetProcAddress(real_sdl, "SDL_EnableScreenSaver");
    p_SDL_EnclosePoints = GetProcAddress(real_sdl, "SDL_EnclosePoints");
    p_SDL_Error = GetProcAddress(real_sdl, "SDL_Error");
    p_SDL_EventState = GetProcAddress(real_sdl, "SDL_EventState");
    p_SDL_FillRect = GetProcAddress(real_sdl, "SDL_FillRect");
    p_SDL_FillRects = GetProcAddress(real_sdl, "SDL_FillRects");
    p_SDL_FilterEvents = GetProcAddress(real_sdl, "SDL_FilterEvents");
    p_SDL_FlushEvent = GetProcAddress(real_sdl, "SDL_FlushEvent");
    p_SDL_FlushEvents = GetProcAddress(real_sdl, "SDL_FlushEvents");
    p_SDL_FreeCursor = GetProcAddress(real_sdl, "SDL_FreeCursor");
    p_SDL_FreeFormat = GetProcAddress(real_sdl, "SDL_FreeFormat");
    p_SDL_FreePalette = GetProcAddress(real_sdl, "SDL_FreePalette");
    p_SDL_FreeRW = GetProcAddress(real_sdl, "SDL_FreeRW");
    p_SDL_FreeSurface = GetProcAddress(real_sdl, "SDL_FreeSurface");
    p_SDL_FreeWAV = GetProcAddress(real_sdl, "SDL_FreeWAV");
    p_SDL_GL_BindTexture = GetProcAddress(real_sdl, "SDL_GL_BindTexture");
    p_SDL_GL_CreateContext = GetProcAddress(real_sdl, "SDL_GL_CreateContext");
    p_SDL_GL_DeleteContext = GetProcAddress(real_sdl, "SDL_GL_DeleteContext");
    p_SDL_GL_ExtensionSupported = GetProcAddress(real_sdl, "SDL_GL_ExtensionSupported");
    p_SDL_GL_GetAttribute = GetProcAddress(real_sdl, "SDL_GL_GetAttribute");
    p_SDL_GL_GetCurrentContext = GetProcAddress(real_sdl, "SDL_GL_GetCurrentContext");
    p_SDL_GL_GetCurrentWindow = GetProcAddress(real_sdl, "SDL_GL_GetCurrentWindow");
    p_SDL_GL_GetDrawableSize = GetProcAddress(real_sdl, "SDL_GL_GetDrawableSize");
    p_SDL_GL_GetProcAddress = GetProcAddress(real_sdl, "SDL_GL_GetProcAddress");
    p_SDL_GL_GetSwapInterval = GetProcAddress(real_sdl, "SDL_GL_GetSwapInterval");
    p_SDL_GL_LoadLibrary = GetProcAddress(real_sdl, "SDL_GL_LoadLibrary");
    p_SDL_GL_MakeCurrent = GetProcAddress(real_sdl, "SDL_GL_MakeCurrent");
    p_SDL_GL_ResetAttributes = GetProcAddress(real_sdl, "SDL_GL_ResetAttributes");
    p_SDL_GL_SetAttribute = GetProcAddress(real_sdl, "SDL_GL_SetAttribute");
    p_SDL_GL_SetSwapInterval = GetProcAddress(real_sdl, "SDL_GL_SetSwapInterval");
    p_SDL_GL_UnbindTexture = GetProcAddress(real_sdl, "SDL_GL_UnbindTexture");
    p_SDL_GL_UnloadLibrary = GetProcAddress(real_sdl, "SDL_GL_UnloadLibrary");
    p_SDL_GameControllerAddMapping = GetProcAddress(real_sdl, "SDL_GameControllerAddMapping");
    p_SDL_GameControllerAddMappingsFromRW = GetProcAddress(real_sdl, "SDL_GameControllerAddMappingsFromRW");
    p_SDL_GameControllerClose = GetProcAddress(real_sdl, "SDL_GameControllerClose");
    p_SDL_GameControllerEventState = GetProcAddress(real_sdl, "SDL_GameControllerEventState");
    p_SDL_GameControllerGetAttached = GetProcAddress(real_sdl, "SDL_GameControllerGetAttached");
    p_SDL_GameControllerGetAxis = GetProcAddress(real_sdl, "SDL_GameControllerGetAxis");
    p_SDL_GameControllerGetAxisFromString = GetProcAddress(real_sdl, "SDL_GameControllerGetAxisFromString");
    p_SDL_GameControllerGetBindForAxis = GetProcAddress(real_sdl, "SDL_GameControllerGetBindForAxis");
    p_SDL_GameControllerGetBindForButton = GetProcAddress(real_sdl, "SDL_GameControllerGetBindForButton");
    p_SDL_GameControllerGetButton = GetProcAddress(real_sdl, "SDL_GameControllerGetButton");
    p_SDL_GameControllerGetButtonFromString = GetProcAddress(real_sdl, "SDL_GameControllerGetButtonFromString");
    p_SDL_GameControllerGetJoystick = GetProcAddress(real_sdl, "SDL_GameControllerGetJoystick");
    p_SDL_GameControllerGetStringForAxis = GetProcAddress(real_sdl, "SDL_GameControllerGetStringForAxis");
    p_SDL_GameControllerGetStringForButton = GetProcAddress(real_sdl, "SDL_GameControllerGetStringForButton");
    p_SDL_GameControllerMapping = GetProcAddress(real_sdl, "SDL_GameControllerMapping");
    p_SDL_GameControllerMappingForGUID = GetProcAddress(real_sdl, "SDL_GameControllerMappingForGUID");
    p_SDL_GameControllerName = GetProcAddress(real_sdl, "SDL_GameControllerName");
    p_SDL_GameControllerNameForIndex = GetProcAddress(real_sdl, "SDL_GameControllerNameForIndex");
    p_SDL_GameControllerOpen = GetProcAddress(real_sdl, "SDL_GameControllerOpen");
    p_SDL_GameControllerUpdate = GetProcAddress(real_sdl, "SDL_GameControllerUpdate");
    p_SDL_GetAssertionHandler = GetProcAddress(real_sdl, "SDL_GetAssertionHandler");
    p_SDL_GetAssertionReport = GetProcAddress(real_sdl, "SDL_GetAssertionReport");
    p_SDL_GetAudioDeviceName = GetProcAddress(real_sdl, "SDL_GetAudioDeviceName");
    p_SDL_GetAudioDeviceStatus = GetProcAddress(real_sdl, "SDL_GetAudioDeviceStatus");
    p_SDL_GetAudioDriver = GetProcAddress(real_sdl, "SDL_GetAudioDriver");
    p_SDL_GetAudioStatus = GetProcAddress(real_sdl, "SDL_GetAudioStatus");
    p_SDL_GetBasePath = GetProcAddress(real_sdl, "SDL_GetBasePath");
    p_SDL_GetCPUCacheLineSize = GetProcAddress(real_sdl, "SDL_GetCPUCacheLineSize");
    p_SDL_GetCPUCount = GetProcAddress(real_sdl, "SDL_GetCPUCount");
    p_SDL_GetClipRect = GetProcAddress(real_sdl, "SDL_GetClipRect");
    p_SDL_GetClipboardText = GetProcAddress(real_sdl, "SDL_GetClipboardText");
    p_SDL_GetClosestDisplayMode = GetProcAddress(real_sdl, "SDL_GetClosestDisplayMode");
    p_SDL_GetColorKey = GetProcAddress(real_sdl, "SDL_GetColorKey");
    p_SDL_GetCurrentAudioDriver = GetProcAddress(real_sdl, "SDL_GetCurrentAudioDriver");
    p_SDL_GetCurrentDisplayMode = GetProcAddress(real_sdl, "SDL_GetCurrentDisplayMode");
    p_SDL_GetCurrentVideoDriver = GetProcAddress(real_sdl, "SDL_GetCurrentVideoDriver");
    p_SDL_GetCursor = GetProcAddress(real_sdl, "SDL_GetCursor");
    p_SDL_GetDefaultAssertionHandler = GetProcAddress(real_sdl, "SDL_GetDefaultAssertionHandler");
    p_SDL_GetDefaultCursor = GetProcAddress(real_sdl, "SDL_GetDefaultCursor");
    p_SDL_GetDesktopDisplayMode = GetProcAddress(real_sdl, "SDL_GetDesktopDisplayMode");
    p_SDL_GetDisplayBounds = GetProcAddress(real_sdl, "SDL_GetDisplayBounds");
    p_SDL_GetDisplayMode = GetProcAddress(real_sdl, "SDL_GetDisplayMode");
    p_SDL_GetDisplayName = GetProcAddress(real_sdl, "SDL_GetDisplayName");
    p_SDL_GetError = GetProcAddress(real_sdl, "SDL_GetError");
    p_SDL_GetEventFilter = GetProcAddress(real_sdl, "SDL_GetEventFilter");
    p_SDL_GetHint = GetProcAddress(real_sdl, "SDL_GetHint");
    p_SDL_GetKeyFromName = GetProcAddress(real_sdl, "SDL_GetKeyFromName");
    p_SDL_GetKeyFromScancode = GetProcAddress(real_sdl, "SDL_GetKeyFromScancode");
    p_SDL_GetKeyName = GetProcAddress(real_sdl, "SDL_GetKeyName");
    p_SDL_GetKeyboardFocus = GetProcAddress(real_sdl, "SDL_GetKeyboardFocus");
    p_SDL_GetKeyboardState = GetProcAddress(real_sdl, "SDL_GetKeyboardState");
    p_SDL_GetModState = GetProcAddress(real_sdl, "SDL_GetModState");
    p_SDL_GetMouseFocus = GetProcAddress(real_sdl, "SDL_GetMouseFocus");
    p_SDL_GetMouseState = GetProcAddress(real_sdl, "SDL_GetMouseState");
    p_SDL_GetNumAudioDevices = GetProcAddress(real_sdl, "SDL_GetNumAudioDevices");
    p_SDL_GetNumAudioDrivers = GetProcAddress(real_sdl, "SDL_GetNumAudioDrivers");
    p_SDL_GetNumDisplayModes = GetProcAddress(real_sdl, "SDL_GetNumDisplayModes");
    p_SDL_GetNumRenderDrivers = GetProcAddress(real_sdl, "SDL_GetNumRenderDrivers");
    p_SDL_GetNumTouchDevices = GetProcAddress(real_sdl, "SDL_GetNumTouchDevices");
    p_SDL_GetNumTouchFingers = GetProcAddress(real_sdl, "SDL_GetNumTouchFingers");
    p_SDL_GetNumVideoDisplays = GetProcAddress(real_sdl, "SDL_GetNumVideoDisplays");
    p_SDL_GetNumVideoDrivers = GetProcAddress(real_sdl, "SDL_GetNumVideoDrivers");
    p_SDL_GetPerformanceCounter = GetProcAddress(real_sdl, "SDL_GetPerformanceCounter");
    p_SDL_GetPerformanceFrequency = GetProcAddress(real_sdl, "SDL_GetPerformanceFrequency");
    p_SDL_GetPixelFormatName = GetProcAddress(real_sdl, "SDL_GetPixelFormatName");
    p_SDL_GetPlatform = GetProcAddress(real_sdl, "SDL_GetPlatform");
    p_SDL_GetPowerInfo = GetProcAddress(real_sdl, "SDL_GetPowerInfo");
    p_SDL_GetPrefPath = GetProcAddress(real_sdl, "SDL_GetPrefPath");
    p_SDL_GetRGB = GetProcAddress(real_sdl, "SDL_GetRGB");
    p_SDL_GetRGBA = GetProcAddress(real_sdl, "SDL_GetRGBA");
    p_SDL_GetRelativeMouseMode = GetProcAddress(real_sdl, "SDL_GetRelativeMouseMode");
    p_SDL_GetRelativeMouseState = GetProcAddress(real_sdl, "SDL_GetRelativeMouseState");
    p_SDL_GetRenderDrawBlendMode = GetProcAddress(real_sdl, "SDL_GetRenderDrawBlendMode");
    p_SDL_GetRenderDrawColor = GetProcAddress(real_sdl, "SDL_GetRenderDrawColor");
    p_SDL_GetRenderDriverInfo = GetProcAddress(real_sdl, "SDL_GetRenderDriverInfo");
    p_SDL_GetRenderTarget = GetProcAddress(real_sdl, "SDL_GetRenderTarget");
    p_SDL_GetRenderer = GetProcAddress(real_sdl, "SDL_GetRenderer");
    p_SDL_GetRendererInfo = GetProcAddress(real_sdl, "SDL_GetRendererInfo");
    p_SDL_GetRendererOutputSize = GetProcAddress(real_sdl, "SDL_GetRendererOutputSize");
    p_SDL_GetRevision = GetProcAddress(real_sdl, "SDL_GetRevision");
    p_SDL_GetRevisionNumber = GetProcAddress(real_sdl, "SDL_GetRevisionNumber");
    p_SDL_GetScancodeFromKey = GetProcAddress(real_sdl, "SDL_GetScancodeFromKey");
    p_SDL_GetScancodeFromName = GetProcAddress(real_sdl, "SDL_GetScancodeFromName");
    p_SDL_GetScancodeName = GetProcAddress(real_sdl, "SDL_GetScancodeName");
    p_SDL_GetShapedWindowMode = GetProcAddress(real_sdl, "SDL_GetShapedWindowMode");
    p_SDL_GetSurfaceAlphaMod = GetProcAddress(real_sdl, "SDL_GetSurfaceAlphaMod");
    p_SDL_GetSurfaceBlendMode = GetProcAddress(real_sdl, "SDL_GetSurfaceBlendMode");
    p_SDL_GetSurfaceColorMod = GetProcAddress(real_sdl, "SDL_GetSurfaceColorMod");
    p_SDL_GetSystemRAM = GetProcAddress(real_sdl, "SDL_GetSystemRAM");
    p_SDL_GetTextureAlphaMod = GetProcAddress(real_sdl, "SDL_GetTextureAlphaMod");
    p_SDL_GetTextureBlendMode = GetProcAddress(real_sdl, "SDL_GetTextureBlendMode");
    p_SDL_GetTextureColorMod = GetProcAddress(real_sdl, "SDL_GetTextureColorMod");
    p_SDL_GetThreadID = GetProcAddress(real_sdl, "SDL_GetThreadID");
    p_SDL_GetThreadName = GetProcAddress(real_sdl, "SDL_GetThreadName");
    p_SDL_GetTicks = GetProcAddress(real_sdl, "SDL_GetTicks");
    p_SDL_GetPerformanceCounter = (void*)luna_SDL_GetPerformanceCounter;
    p_SDL_GetPerformanceFrequency = (void*)luna_SDL_GetPerformanceFrequency;
    p_SDL_GetTicks = (void*)luna_SDL_GetTicks;
    p_SDL_GetTouchDevice = GetProcAddress(real_sdl, "SDL_GetTouchDevice");
    p_SDL_GetTouchFinger = GetProcAddress(real_sdl, "SDL_GetTouchFinger");
    p_SDL_GetVersion = GetProcAddress(real_sdl, "SDL_GetVersion");
    p_SDL_GetVideoDriver = GetProcAddress(real_sdl, "SDL_GetVideoDriver");
    p_SDL_GetWindowBrightness = GetProcAddress(real_sdl, "SDL_GetWindowBrightness");
    p_SDL_GetWindowData = GetProcAddress(real_sdl, "SDL_GetWindowData");
    p_SDL_GetWindowDisplayIndex = GetProcAddress(real_sdl, "SDL_GetWindowDisplayIndex");
    p_SDL_GetWindowDisplayMode = GetProcAddress(real_sdl, "SDL_GetWindowDisplayMode");
    p_SDL_GetWindowFlags = GetProcAddress(real_sdl, "SDL_GetWindowFlags");
    p_SDL_GetWindowFromID = GetProcAddress(real_sdl, "SDL_GetWindowFromID");
    p_SDL_GetWindowGammaRamp = GetProcAddress(real_sdl, "SDL_GetWindowGammaRamp");
    p_SDL_GetWindowGrab = GetProcAddress(real_sdl, "SDL_GetWindowGrab");
    p_SDL_GetWindowID = GetProcAddress(real_sdl, "SDL_GetWindowID");
    p_SDL_GetWindowMaximumSize = GetProcAddress(real_sdl, "SDL_GetWindowMaximumSize");
    p_SDL_GetWindowMinimumSize = GetProcAddress(real_sdl, "SDL_GetWindowMinimumSize");
    p_SDL_GetWindowPixelFormat = GetProcAddress(real_sdl, "SDL_GetWindowPixelFormat");
    p_SDL_GetWindowPosition = GetProcAddress(real_sdl, "SDL_GetWindowPosition");
    p_SDL_GetWindowSize = GetProcAddress(real_sdl, "SDL_GetWindowSize");
    p_SDL_GetWindowSurface = GetProcAddress(real_sdl, "SDL_GetWindowSurface");
    p_SDL_GetWindowTitle = GetProcAddress(real_sdl, "SDL_GetWindowTitle");
    p_SDL_GetWindowWMInfo = GetProcAddress(real_sdl, "SDL_GetWindowWMInfo");
    p_SDL_HapticClose = GetProcAddress(real_sdl, "SDL_HapticClose");
    p_SDL_HapticDestroyEffect = GetProcAddress(real_sdl, "SDL_HapticDestroyEffect");
    p_SDL_HapticEffectSupported = GetProcAddress(real_sdl, "SDL_HapticEffectSupported");
    p_SDL_HapticGetEffectStatus = GetProcAddress(real_sdl, "SDL_HapticGetEffectStatus");
    p_SDL_HapticIndex = GetProcAddress(real_sdl, "SDL_HapticIndex");
    p_SDL_HapticName = GetProcAddress(real_sdl, "SDL_HapticName");
    p_SDL_HapticNewEffect = GetProcAddress(real_sdl, "SDL_HapticNewEffect");
    p_SDL_HapticNumAxes = GetProcAddress(real_sdl, "SDL_HapticNumAxes");
    p_SDL_HapticNumEffects = GetProcAddress(real_sdl, "SDL_HapticNumEffects");
    p_SDL_HapticNumEffectsPlaying = GetProcAddress(real_sdl, "SDL_HapticNumEffectsPlaying");
    p_SDL_HapticOpen = GetProcAddress(real_sdl, "SDL_HapticOpen");
    p_SDL_HapticOpenFromJoystick = GetProcAddress(real_sdl, "SDL_HapticOpenFromJoystick");
    p_SDL_HapticOpenFromMouse = GetProcAddress(real_sdl, "SDL_HapticOpenFromMouse");
    p_SDL_HapticOpened = GetProcAddress(real_sdl, "SDL_HapticOpened");
    p_SDL_HapticPause = GetProcAddress(real_sdl, "SDL_HapticPause");
    p_SDL_HapticQuery = GetProcAddress(real_sdl, "SDL_HapticQuery");
    p_SDL_HapticRumbleInit = GetProcAddress(real_sdl, "SDL_HapticRumbleInit");
    p_SDL_HapticRumblePlay = GetProcAddress(real_sdl, "SDL_HapticRumblePlay");
    p_SDL_HapticRumbleStop = GetProcAddress(real_sdl, "SDL_HapticRumbleStop");
    p_SDL_HapticRumbleSupported = GetProcAddress(real_sdl, "SDL_HapticRumbleSupported");
    p_SDL_HapticRunEffect = GetProcAddress(real_sdl, "SDL_HapticRunEffect");
    p_SDL_HapticSetAutocenter = GetProcAddress(real_sdl, "SDL_HapticSetAutocenter");
    p_SDL_HapticSetGain = GetProcAddress(real_sdl, "SDL_HapticSetGain");
    p_SDL_HapticStopAll = GetProcAddress(real_sdl, "SDL_HapticStopAll");
    p_SDL_HapticStopEffect = GetProcAddress(real_sdl, "SDL_HapticStopEffect");
    p_SDL_HapticUnpause = GetProcAddress(real_sdl, "SDL_HapticUnpause");
    p_SDL_HapticUpdateEffect = GetProcAddress(real_sdl, "SDL_HapticUpdateEffect");
    p_SDL_Has3DNow = GetProcAddress(real_sdl, "SDL_Has3DNow");
    p_SDL_HasAVX = GetProcAddress(real_sdl, "SDL_HasAVX");
    p_SDL_HasAltiVec = GetProcAddress(real_sdl, "SDL_HasAltiVec");
    p_SDL_HasClipboardText = GetProcAddress(real_sdl, "SDL_HasClipboardText");
    p_SDL_HasEvent = GetProcAddress(real_sdl, "SDL_HasEvent");
    p_SDL_HasEvents = GetProcAddress(real_sdl, "SDL_HasEvents");
    p_SDL_HasIntersection = GetProcAddress(real_sdl, "SDL_HasIntersection");
    p_SDL_HasMMX = GetProcAddress(real_sdl, "SDL_HasMMX");
    p_SDL_HasRDTSC = GetProcAddress(real_sdl, "SDL_HasRDTSC");
    p_SDL_HasSSE = GetProcAddress(real_sdl, "SDL_HasSSE");
    p_SDL_HasSSE2 = GetProcAddress(real_sdl, "SDL_HasSSE2");
    p_SDL_HasSSE3 = GetProcAddress(real_sdl, "SDL_HasSSE3");
    p_SDL_HasSSE41 = GetProcAddress(real_sdl, "SDL_HasSSE41");
    p_SDL_HasSSE42 = GetProcAddress(real_sdl, "SDL_HasSSE42");
    p_SDL_HasScreenKeyboardSupport = GetProcAddress(real_sdl, "SDL_HasScreenKeyboardSupport");
    p_SDL_HideWindow = GetProcAddress(real_sdl, "SDL_HideWindow");
    p_SDL_Init = GetProcAddress(real_sdl, "SDL_Init");
    p_SDL_InitSubSystem = GetProcAddress(real_sdl, "SDL_InitSubSystem");
    p_SDL_IntersectRect = GetProcAddress(real_sdl, "SDL_IntersectRect");
    p_SDL_IntersectRectAndLine = GetProcAddress(real_sdl, "SDL_IntersectRectAndLine");
    p_SDL_IsGameController = GetProcAddress(real_sdl, "SDL_IsGameController");
    p_SDL_IsScreenKeyboardShown = GetProcAddress(real_sdl, "SDL_IsScreenKeyboardShown");
    p_SDL_IsScreenSaverEnabled = GetProcAddress(real_sdl, "SDL_IsScreenSaverEnabled");
    p_SDL_IsShapedWindow = GetProcAddress(real_sdl, "SDL_IsShapedWindow");
    p_SDL_IsTextInputActive = GetProcAddress(real_sdl, "SDL_IsTextInputActive");
    p_SDL_JoystickClose = GetProcAddress(real_sdl, "SDL_JoystickClose");
    p_SDL_JoystickEventState = GetProcAddress(real_sdl, "SDL_JoystickEventState");
    p_SDL_JoystickGetAttached = GetProcAddress(real_sdl, "SDL_JoystickGetAttached");
    p_SDL_JoystickGetAxis = GetProcAddress(real_sdl, "SDL_JoystickGetAxis");
    p_SDL_JoystickGetBall = GetProcAddress(real_sdl, "SDL_JoystickGetBall");
    p_SDL_JoystickGetButton = GetProcAddress(real_sdl, "SDL_JoystickGetButton");
    p_SDL_JoystickGetDeviceGUID = GetProcAddress(real_sdl, "SDL_JoystickGetDeviceGUID");
    p_SDL_JoystickGetGUID = GetProcAddress(real_sdl, "SDL_JoystickGetGUID");
    p_SDL_JoystickGetGUIDFromString = GetProcAddress(real_sdl, "SDL_JoystickGetGUIDFromString");
    p_SDL_JoystickGetGUIDString = GetProcAddress(real_sdl, "SDL_JoystickGetGUIDString");
    p_SDL_JoystickGetHat = GetProcAddress(real_sdl, "SDL_JoystickGetHat");
    p_SDL_JoystickInstanceID = GetProcAddress(real_sdl, "SDL_JoystickInstanceID");
    p_SDL_JoystickIsHaptic = GetProcAddress(real_sdl, "SDL_JoystickIsHaptic");
    p_SDL_JoystickName = GetProcAddress(real_sdl, "SDL_JoystickName");
    p_SDL_JoystickNameForIndex = GetProcAddress(real_sdl, "SDL_JoystickNameForIndex");
    p_SDL_JoystickNumAxes = GetProcAddress(real_sdl, "SDL_JoystickNumAxes");
    p_SDL_JoystickNumBalls = GetProcAddress(real_sdl, "SDL_JoystickNumBalls");
    p_SDL_JoystickNumButtons = GetProcAddress(real_sdl, "SDL_JoystickNumButtons");
    p_SDL_JoystickNumHats = GetProcAddress(real_sdl, "SDL_JoystickNumHats");
    p_SDL_JoystickOpen = GetProcAddress(real_sdl, "SDL_JoystickOpen");
    p_SDL_JoystickUpdate = GetProcAddress(real_sdl, "SDL_JoystickUpdate");
    p_SDL_LoadBMP_RW = GetProcAddress(real_sdl, "SDL_LoadBMP_RW");
    p_SDL_LoadDollarTemplates = GetProcAddress(real_sdl, "SDL_LoadDollarTemplates");
    p_SDL_LoadFunction = GetProcAddress(real_sdl, "SDL_LoadFunction");
    p_SDL_LoadObject = GetProcAddress(real_sdl, "SDL_LoadObject");
    p_SDL_LoadWAV_RW = GetProcAddress(real_sdl, "SDL_LoadWAV_RW");
    p_SDL_LockAudio = GetProcAddress(real_sdl, "SDL_LockAudio");
    p_SDL_LockAudioDevice = GetProcAddress(real_sdl, "SDL_LockAudioDevice");
    p_SDL_LockMutex = GetProcAddress(real_sdl, "SDL_LockMutex");
    p_SDL_LockSurface = GetProcAddress(real_sdl, "SDL_LockSurface");
    p_SDL_LockTexture = GetProcAddress(real_sdl, "SDL_LockTexture");
    p_SDL_Log = GetProcAddress(real_sdl, "SDL_Log");
    p_SDL_LogCritical = GetProcAddress(real_sdl, "SDL_LogCritical");
    p_SDL_LogDebug = GetProcAddress(real_sdl, "SDL_LogDebug");
    p_SDL_LogError = GetProcAddress(real_sdl, "SDL_LogError");
    p_SDL_LogGetOutputFunction = GetProcAddress(real_sdl, "SDL_LogGetOutputFunction");
    p_SDL_LogGetPriority = GetProcAddress(real_sdl, "SDL_LogGetPriority");
    p_SDL_LogInfo = GetProcAddress(real_sdl, "SDL_LogInfo");
    p_SDL_LogMessage = GetProcAddress(real_sdl, "SDL_LogMessage");
    p_SDL_LogMessageV = GetProcAddress(real_sdl, "SDL_LogMessageV");
    p_SDL_LogResetPriorities = GetProcAddress(real_sdl, "SDL_LogResetPriorities");
    p_SDL_LogSetAllPriority = GetProcAddress(real_sdl, "SDL_LogSetAllPriority");
    p_SDL_LogSetOutputFunction = GetProcAddress(real_sdl, "SDL_LogSetOutputFunction");
    p_SDL_LogSetPriority = GetProcAddress(real_sdl, "SDL_LogSetPriority");
    p_SDL_LogVerbose = GetProcAddress(real_sdl, "SDL_LogVerbose");
    p_SDL_LogWarn = GetProcAddress(real_sdl, "SDL_LogWarn");
    p_SDL_LowerBlit = GetProcAddress(real_sdl, "SDL_LowerBlit");
    p_SDL_LowerBlitScaled = GetProcAddress(real_sdl, "SDL_LowerBlitScaled");
    p_SDL_MapRGB = GetProcAddress(real_sdl, "SDL_MapRGB");
    p_SDL_MapRGBA = GetProcAddress(real_sdl, "SDL_MapRGBA");
    p_SDL_MasksToPixelFormatEnum = GetProcAddress(real_sdl, "SDL_MasksToPixelFormatEnum");
    p_SDL_MaximizeWindow = GetProcAddress(real_sdl, "SDL_MaximizeWindow");
    p_SDL_MinimizeWindow = GetProcAddress(real_sdl, "SDL_MinimizeWindow");
    p_SDL_MixAudio = GetProcAddress(real_sdl, "SDL_MixAudio");
    p_SDL_MixAudioFormat = GetProcAddress(real_sdl, "SDL_MixAudioFormat");
    p_SDL_MouseIsHaptic = GetProcAddress(real_sdl, "SDL_MouseIsHaptic");
    p_SDL_NumHaptics = GetProcAddress(real_sdl, "SDL_NumHaptics");
    p_SDL_NumJoysticks = GetProcAddress(real_sdl, "SDL_NumJoysticks");
    p_SDL_OpenAudio = GetProcAddress(real_sdl, "SDL_OpenAudio");
    p_SDL_OpenAudioDevice = GetProcAddress(real_sdl, "SDL_OpenAudioDevice");
    p_SDL_PauseAudio = GetProcAddress(real_sdl, "SDL_PauseAudio");
    p_SDL_PauseAudioDevice = GetProcAddress(real_sdl, "SDL_PauseAudioDevice");
    p_SDL_PeepEvents = GetProcAddress(real_sdl, "SDL_PeepEvents");
    p_SDL_PixelFormatEnumToMasks = GetProcAddress(real_sdl, "SDL_PixelFormatEnumToMasks");
    p_SDL_PumpEvents = GetProcAddress(real_sdl, "SDL_PumpEvents");
    p_SDL_PushEvent = GetProcAddress(real_sdl, "SDL_PushEvent");
    p_SDL_QueryTexture = GetProcAddress(real_sdl, "SDL_QueryTexture");
    p_SDL_Quit = GetProcAddress(real_sdl, "SDL_Quit");
    p_SDL_QuitSubSystem = GetProcAddress(real_sdl, "SDL_QuitSubSystem");
    p_SDL_RWFromConstMem = GetProcAddress(real_sdl, "SDL_RWFromConstMem");
    p_SDL_RWFromFP = GetProcAddress(real_sdl, "SDL_RWFromFP");
    p_SDL_RWFromFile = GetProcAddress(real_sdl, "SDL_RWFromFile");
    p_SDL_RWFromMem = GetProcAddress(real_sdl, "SDL_RWFromMem");
    p_SDL_RaiseWindow = GetProcAddress(real_sdl, "SDL_RaiseWindow");
    p_SDL_ReadBE16 = GetProcAddress(real_sdl, "SDL_ReadBE16");
    p_SDL_ReadBE32 = GetProcAddress(real_sdl, "SDL_ReadBE32");
    p_SDL_ReadBE64 = GetProcAddress(real_sdl, "SDL_ReadBE64");
    p_SDL_ReadLE16 = GetProcAddress(real_sdl, "SDL_ReadLE16");
    p_SDL_ReadLE32 = GetProcAddress(real_sdl, "SDL_ReadLE32");
    p_SDL_ReadLE64 = GetProcAddress(real_sdl, "SDL_ReadLE64");
    p_SDL_ReadU8 = GetProcAddress(real_sdl, "SDL_ReadU8");
    p_SDL_RecordGesture = GetProcAddress(real_sdl, "SDL_RecordGesture");
    p_SDL_RegisterApp = GetProcAddress(real_sdl, "SDL_RegisterApp");
    p_SDL_RegisterEvents = GetProcAddress(real_sdl, "SDL_RegisterEvents");
    p_SDL_RemoveTimer = GetProcAddress(real_sdl, "SDL_RemoveTimer");
    p_SDL_RenderClear = GetProcAddress(real_sdl, "SDL_RenderClear");
    p_SDL_RenderCopy = GetProcAddress(real_sdl, "SDL_RenderCopy");
    p_SDL_RenderCopyEx = GetProcAddress(real_sdl, "SDL_RenderCopyEx");
    p_SDL_RenderDrawLine = GetProcAddress(real_sdl, "SDL_RenderDrawLine");
    p_SDL_RenderDrawLines = GetProcAddress(real_sdl, "SDL_RenderDrawLines");
    p_SDL_RenderDrawPoint = GetProcAddress(real_sdl, "SDL_RenderDrawPoint");
    p_SDL_RenderDrawPoints = GetProcAddress(real_sdl, "SDL_RenderDrawPoints");
    p_SDL_RenderDrawRect = GetProcAddress(real_sdl, "SDL_RenderDrawRect");
    p_SDL_RenderDrawRects = GetProcAddress(real_sdl, "SDL_RenderDrawRects");
    p_SDL_RenderFillRect = GetProcAddress(real_sdl, "SDL_RenderFillRect");
    p_SDL_RenderFillRects = GetProcAddress(real_sdl, "SDL_RenderFillRects");
    p_SDL_RenderGetClipRect = GetProcAddress(real_sdl, "SDL_RenderGetClipRect");
    p_SDL_RenderGetD3D9Device = GetProcAddress(real_sdl, "SDL_RenderGetD3D9Device");
    p_SDL_RenderGetLogicalSize = GetProcAddress(real_sdl, "SDL_RenderGetLogicalSize");
    p_SDL_RenderGetScale = GetProcAddress(real_sdl, "SDL_RenderGetScale");
    p_SDL_RenderGetViewport = GetProcAddress(real_sdl, "SDL_RenderGetViewport");
    p_SDL_RenderPresent = GetProcAddress(real_sdl, "SDL_RenderPresent");
    p_SDL_RenderReadPixels = GetProcAddress(real_sdl, "SDL_RenderReadPixels");
    p_SDL_RenderSetClipRect = GetProcAddress(real_sdl, "SDL_RenderSetClipRect");
    p_SDL_RenderSetLogicalSize = GetProcAddress(real_sdl, "SDL_RenderSetLogicalSize");
    p_SDL_RenderSetScale = GetProcAddress(real_sdl, "SDL_RenderSetScale");
    p_SDL_RenderSetViewport = GetProcAddress(real_sdl, "SDL_RenderSetViewport");
    p_SDL_RenderTargetSupported = GetProcAddress(real_sdl, "SDL_RenderTargetSupported");
    p_SDL_ReportAssertion = GetProcAddress(real_sdl, "SDL_ReportAssertion");
    p_SDL_ResetAssertionReport = GetProcAddress(real_sdl, "SDL_ResetAssertionReport");
    p_SDL_RestoreWindow = GetProcAddress(real_sdl, "SDL_RestoreWindow");
    p_SDL_SaveAllDollarTemplates = GetProcAddress(real_sdl, "SDL_SaveAllDollarTemplates");
    p_SDL_SaveBMP_RW = GetProcAddress(real_sdl, "SDL_SaveBMP_RW");
    p_SDL_SaveDollarTemplate = GetProcAddress(real_sdl, "SDL_SaveDollarTemplate");
    p_SDL_SemPost = GetProcAddress(real_sdl, "SDL_SemPost");
    p_SDL_SemTryWait = GetProcAddress(real_sdl, "SDL_SemTryWait");
    p_SDL_SemValue = GetProcAddress(real_sdl, "SDL_SemValue");
    p_SDL_SemWait = GetProcAddress(real_sdl, "SDL_SemWait");
    p_SDL_SemWaitTimeout = GetProcAddress(real_sdl, "SDL_SemWaitTimeout");
    p_SDL_SetAssertionHandler = GetProcAddress(real_sdl, "SDL_SetAssertionHandler");
    p_SDL_SetClipRect = GetProcAddress(real_sdl, "SDL_SetClipRect");
    p_SDL_SetClipboardText = GetProcAddress(real_sdl, "SDL_SetClipboardText");
    p_SDL_SetColorKey = GetProcAddress(real_sdl, "SDL_SetColorKey");
    p_SDL_SetCursor = GetProcAddress(real_sdl, "SDL_SetCursor");
    p_SDL_SetError = GetProcAddress(real_sdl, "SDL_SetError");
    p_SDL_SetEventFilter = GetProcAddress(real_sdl, "SDL_SetEventFilter");
    p_SDL_SetHint = GetProcAddress(real_sdl, "SDL_SetHint");
    p_SDL_SetHintWithPriority = GetProcAddress(real_sdl, "SDL_SetHintWithPriority");
    p_SDL_SetMainReady = GetProcAddress(real_sdl, "SDL_SetMainReady");
    p_SDL_SetModState = GetProcAddress(real_sdl, "SDL_SetModState");
    p_SDL_SetPaletteColors = GetProcAddress(real_sdl, "SDL_SetPaletteColors");
    p_SDL_SetPixelFormatPalette = GetProcAddress(real_sdl, "SDL_SetPixelFormatPalette");
    p_SDL_SetRelativeMouseMode = GetProcAddress(real_sdl, "SDL_SetRelativeMouseMode");
    p_SDL_SetRenderDrawBlendMode = GetProcAddress(real_sdl, "SDL_SetRenderDrawBlendMode");
    p_SDL_SetRenderDrawColor = GetProcAddress(real_sdl, "SDL_SetRenderDrawColor");
    p_SDL_SetRenderTarget = GetProcAddress(real_sdl, "SDL_SetRenderTarget");
    p_SDL_SetSurfaceAlphaMod = GetProcAddress(real_sdl, "SDL_SetSurfaceAlphaMod");
    p_SDL_SetSurfaceBlendMode = GetProcAddress(real_sdl, "SDL_SetSurfaceBlendMode");
    p_SDL_SetSurfaceColorMod = GetProcAddress(real_sdl, "SDL_SetSurfaceColorMod");
    p_SDL_SetSurfacePalette = GetProcAddress(real_sdl, "SDL_SetSurfacePalette");
    p_SDL_SetSurfaceRLE = GetProcAddress(real_sdl, "SDL_SetSurfaceRLE");
    p_SDL_SetTextInputRect = GetProcAddress(real_sdl, "SDL_SetTextInputRect");
    p_SDL_SetTextureAlphaMod = GetProcAddress(real_sdl, "SDL_SetTextureAlphaMod");
    p_SDL_SetTextureBlendMode = GetProcAddress(real_sdl, "SDL_SetTextureBlendMode");
    p_SDL_SetTextureColorMod = GetProcAddress(real_sdl, "SDL_SetTextureColorMod");
    p_SDL_SetThreadPriority = GetProcAddress(real_sdl, "SDL_SetThreadPriority");
    p_SDL_SetWindowBordered = GetProcAddress(real_sdl, "SDL_SetWindowBordered");
    p_SDL_SetWindowBrightness = GetProcAddress(real_sdl, "SDL_SetWindowBrightness");
    p_SDL_SetWindowData = GetProcAddress(real_sdl, "SDL_SetWindowData");
    p_SDL_SetWindowDisplayMode = GetProcAddress(real_sdl, "SDL_SetWindowDisplayMode");
    p_SDL_SetWindowFullscreen = GetProcAddress(real_sdl, "SDL_SetWindowFullscreen");
    p_SDL_SetWindowGammaRamp = GetProcAddress(real_sdl, "SDL_SetWindowGammaRamp");
    p_SDL_SetWindowGrab = GetProcAddress(real_sdl, "SDL_SetWindowGrab");
    p_SDL_SetWindowIcon = GetProcAddress(real_sdl, "SDL_SetWindowIcon");
    p_SDL_SetWindowMaximumSize = GetProcAddress(real_sdl, "SDL_SetWindowMaximumSize");
    p_SDL_SetWindowMinimumSize = GetProcAddress(real_sdl, "SDL_SetWindowMinimumSize");
    p_SDL_SetWindowPosition = GetProcAddress(real_sdl, "SDL_SetWindowPosition");
    p_SDL_SetWindowShape = GetProcAddress(real_sdl, "SDL_SetWindowShape");
    p_SDL_SetWindowSize = GetProcAddress(real_sdl, "SDL_SetWindowSize");
    p_SDL_SetWindowTitle = GetProcAddress(real_sdl, "SDL_SetWindowTitle");
    p_SDL_ShowCursor = GetProcAddress(real_sdl, "SDL_ShowCursor");
    p_SDL_ShowMessageBox = GetProcAddress(real_sdl, "SDL_ShowMessageBox");
    p_SDL_ShowSimpleMessageBox = GetProcAddress(real_sdl, "SDL_ShowSimpleMessageBox");
    p_SDL_ShowWindow = GetProcAddress(real_sdl, "SDL_ShowWindow");
    p_SDL_SoftStretch = GetProcAddress(real_sdl, "SDL_SoftStretch");
    p_SDL_StartTextInput = GetProcAddress(real_sdl, "SDL_StartTextInput");
    p_SDL_StopTextInput = GetProcAddress(real_sdl, "SDL_StopTextInput");
    p_SDL_TLSCreate = GetProcAddress(real_sdl, "SDL_TLSCreate");
    p_SDL_TLSGet = GetProcAddress(real_sdl, "SDL_TLSGet");
    p_SDL_TLSSet = GetProcAddress(real_sdl, "SDL_TLSSet");
    p_SDL_ThreadID = GetProcAddress(real_sdl, "SDL_ThreadID");
    p_SDL_TryLockMutex = GetProcAddress(real_sdl, "SDL_TryLockMutex");
    p_SDL_UnionRect = GetProcAddress(real_sdl, "SDL_UnionRect");
    p_SDL_UnloadObject = GetProcAddress(real_sdl, "SDL_UnloadObject");
    p_SDL_UnlockAudio = GetProcAddress(real_sdl, "SDL_UnlockAudio");
    p_SDL_UnlockAudioDevice = GetProcAddress(real_sdl, "SDL_UnlockAudioDevice");
    p_SDL_UnlockMutex = GetProcAddress(real_sdl, "SDL_UnlockMutex");
    p_SDL_UnlockSurface = GetProcAddress(real_sdl, "SDL_UnlockSurface");
    p_SDL_UnlockTexture = GetProcAddress(real_sdl, "SDL_UnlockTexture");
    p_SDL_UnregisterApp = GetProcAddress(real_sdl, "SDL_UnregisterApp");
    p_SDL_UpdateTexture = GetProcAddress(real_sdl, "SDL_UpdateTexture");
    p_SDL_UpdateWindowSurface = GetProcAddress(real_sdl, "SDL_UpdateWindowSurface");
    p_SDL_UpdateWindowSurfaceRects = GetProcAddress(real_sdl, "SDL_UpdateWindowSurfaceRects");
    p_SDL_UpdateYUVTexture = GetProcAddress(real_sdl, "SDL_UpdateYUVTexture");
    p_SDL_UpperBlit = GetProcAddress(real_sdl, "SDL_UpperBlit");
    p_SDL_UpperBlitScaled = GetProcAddress(real_sdl, "SDL_UpperBlitScaled");
    p_SDL_VideoInit = GetProcAddress(real_sdl, "SDL_VideoInit");
    p_SDL_VideoQuit = GetProcAddress(real_sdl, "SDL_VideoQuit");
    p_SDL_WaitEvent = GetProcAddress(real_sdl, "SDL_WaitEvent");
    p_SDL_WaitEventTimeout = GetProcAddress(real_sdl, "SDL_WaitEventTimeout");
    p_SDL_WaitThread = GetProcAddress(real_sdl, "SDL_WaitThread");
    p_SDL_WarpMouseInWindow = GetProcAddress(real_sdl, "SDL_WarpMouseInWindow");
    p_SDL_WasInit = GetProcAddress(real_sdl, "SDL_WasInit");
    p_SDL_WriteBE16 = GetProcAddress(real_sdl, "SDL_WriteBE16");
    p_SDL_WriteBE32 = GetProcAddress(real_sdl, "SDL_WriteBE32");
    p_SDL_WriteBE64 = GetProcAddress(real_sdl, "SDL_WriteBE64");
    p_SDL_WriteLE16 = GetProcAddress(real_sdl, "SDL_WriteLE16");
    p_SDL_WriteLE32 = GetProcAddress(real_sdl, "SDL_WriteLE32");
    p_SDL_WriteLE64 = GetProcAddress(real_sdl, "SDL_WriteLE64");
    p_SDL_WriteU8 = GetProcAddress(real_sdl, "SDL_WriteU8");
    p_SDL_abs = GetProcAddress(real_sdl, "SDL_abs");
    p_SDL_acos = GetProcAddress(real_sdl, "SDL_acos");
    p_SDL_asin = GetProcAddress(real_sdl, "SDL_asin");
    p_SDL_atan = GetProcAddress(real_sdl, "SDL_atan");
    p_SDL_atan2 = GetProcAddress(real_sdl, "SDL_atan2");
    p_SDL_atof = GetProcAddress(real_sdl, "SDL_atof");
    p_SDL_atoi = GetProcAddress(real_sdl, "SDL_atoi");
    p_SDL_calloc = GetProcAddress(real_sdl, "SDL_calloc");
    p_SDL_ceil = GetProcAddress(real_sdl, "SDL_ceil");
    p_SDL_copysign = GetProcAddress(real_sdl, "SDL_copysign");
    p_SDL_cos = GetProcAddress(real_sdl, "SDL_cos");
    p_SDL_cosf = GetProcAddress(real_sdl, "SDL_cosf");
    p_SDL_fabs = GetProcAddress(real_sdl, "SDL_fabs");
    p_SDL_floor = GetProcAddress(real_sdl, "SDL_floor");
    p_SDL_free = GetProcAddress(real_sdl, "SDL_free");
    p_SDL_getenv = GetProcAddress(real_sdl, "SDL_getenv");
    p_SDL_iconv = GetProcAddress(real_sdl, "SDL_iconv");
    p_SDL_iconv_close = GetProcAddress(real_sdl, "SDL_iconv_close");
    p_SDL_iconv_open = GetProcAddress(real_sdl, "SDL_iconv_open");
    p_SDL_iconv_string = GetProcAddress(real_sdl, "SDL_iconv_string");
    p_SDL_isdigit = GetProcAddress(real_sdl, "SDL_isdigit");
    p_SDL_isspace = GetProcAddress(real_sdl, "SDL_isspace");
    p_SDL_itoa = GetProcAddress(real_sdl, "SDL_itoa");
    p_SDL_lltoa = GetProcAddress(real_sdl, "SDL_lltoa");
    p_SDL_log = GetProcAddress(real_sdl, "SDL_log");
    p_SDL_ltoa = GetProcAddress(real_sdl, "SDL_ltoa");
    p_SDL_malloc = GetProcAddress(real_sdl, "SDL_malloc");
    p_SDL_memcmp = GetProcAddress(real_sdl, "SDL_memcmp");
    p_SDL_memcpy = GetProcAddress(real_sdl, "SDL_memcpy");
    p_SDL_memmove = GetProcAddress(real_sdl, "SDL_memmove");
    p_SDL_memset = GetProcAddress(real_sdl, "SDL_memset");
    p_SDL_pow = GetProcAddress(real_sdl, "SDL_pow");
    p_SDL_qsort = GetProcAddress(real_sdl, "SDL_qsort");
    p_SDL_realloc = GetProcAddress(real_sdl, "SDL_realloc");
    p_SDL_scalbn = GetProcAddress(real_sdl, "SDL_scalbn");
    p_SDL_setenv = GetProcAddress(real_sdl, "SDL_setenv");
    p_SDL_sin = GetProcAddress(real_sdl, "SDL_sin");
    p_SDL_sinf = GetProcAddress(real_sdl, "SDL_sinf");
    p_SDL_snprintf = GetProcAddress(real_sdl, "SDL_snprintf");
    p_SDL_sqrt = GetProcAddress(real_sdl, "SDL_sqrt");
    p_SDL_sscanf = GetProcAddress(real_sdl, "SDL_sscanf");
    p_SDL_strcasecmp = GetProcAddress(real_sdl, "SDL_strcasecmp");
    p_SDL_strchr = GetProcAddress(real_sdl, "SDL_strchr");
    p_SDL_strcmp = GetProcAddress(real_sdl, "SDL_strcmp");
    p_SDL_strdup = GetProcAddress(real_sdl, "SDL_strdup");
    p_SDL_strlcat = GetProcAddress(real_sdl, "SDL_strlcat");
    p_SDL_strlcpy = GetProcAddress(real_sdl, "SDL_strlcpy");
    p_SDL_strlen = GetProcAddress(real_sdl, "SDL_strlen");
    p_SDL_strlwr = GetProcAddress(real_sdl, "SDL_strlwr");
    p_SDL_strncasecmp = GetProcAddress(real_sdl, "SDL_strncasecmp");
    p_SDL_strncmp = GetProcAddress(real_sdl, "SDL_strncmp");
    p_SDL_strrchr = GetProcAddress(real_sdl, "SDL_strrchr");
    p_SDL_strrev = GetProcAddress(real_sdl, "SDL_strrev");
    p_SDL_strstr = GetProcAddress(real_sdl, "SDL_strstr");
    p_SDL_strtod = GetProcAddress(real_sdl, "SDL_strtod");
    p_SDL_strtol = GetProcAddress(real_sdl, "SDL_strtol");
    p_SDL_strtoll = GetProcAddress(real_sdl, "SDL_strtoll");
    p_SDL_strtoul = GetProcAddress(real_sdl, "SDL_strtoul");
    p_SDL_strtoull = GetProcAddress(real_sdl, "SDL_strtoull");
    p_SDL_strupr = GetProcAddress(real_sdl, "SDL_strupr");
    p_SDL_tolower = GetProcAddress(real_sdl, "SDL_tolower");
    p_SDL_toupper = GetProcAddress(real_sdl, "SDL_toupper");
    p_SDL_uitoa = GetProcAddress(real_sdl, "SDL_uitoa");
    p_SDL_ulltoa = GetProcAddress(real_sdl, "SDL_ulltoa");
    p_SDL_ultoa = GetProcAddress(real_sdl, "SDL_ultoa");
    p_SDL_utf8strlcpy = GetProcAddress(real_sdl, "SDL_utf8strlcpy");
    p_SDL_vsnprintf = GetProcAddress(real_sdl, "SDL_vsnprintf");
    p_SDL_vsscanf = GetProcAddress(real_sdl, "SDL_vsscanf");
    p_SDL_wcslcat = GetProcAddress(real_sdl, "SDL_wcslcat");
    p_SDL_wcslcpy = GetProcAddress(real_sdl, "SDL_wcslcpy");
    p_SDL_wcslen = GetProcAddress(real_sdl, "SDL_wcslen");
}

__attribute__((naked)) void SDL_AddEventWatch() { asm("jmp *%0" : : "m"(p_SDL_AddEventWatch)); }
__attribute__((naked)) void SDL_AddHintCallback() { asm("jmp *%0" : : "m"(p_SDL_AddHintCallback)); }
__attribute__((naked)) void SDL_AddTimer() { asm("jmp *%0" : : "m"(p_SDL_AddTimer)); }
__attribute__((naked)) void SDL_AllocFormat() { asm("jmp *%0" : : "m"(p_SDL_AllocFormat)); }
__attribute__((naked)) void SDL_AllocPalette() { asm("jmp *%0" : : "m"(p_SDL_AllocPalette)); }
__attribute__((naked)) void SDL_AllocRW() { asm("jmp *%0" : : "m"(p_SDL_AllocRW)); }
__attribute__((naked)) void SDL_AtomicAdd() { asm("jmp *%0" : : "m"(p_SDL_AtomicAdd)); }
__attribute__((naked)) void SDL_AtomicCAS() { asm("jmp *%0" : : "m"(p_SDL_AtomicCAS)); }
__attribute__((naked)) void SDL_AtomicCASPtr() { asm("jmp *%0" : : "m"(p_SDL_AtomicCASPtr)); }
__attribute__((naked)) void SDL_AtomicGet() { asm("jmp *%0" : : "m"(p_SDL_AtomicGet)); }
__attribute__((naked)) void SDL_AtomicGetPtr() { asm("jmp *%0" : : "m"(p_SDL_AtomicGetPtr)); }
__attribute__((naked)) void SDL_AtomicLock() { asm("jmp *%0" : : "m"(p_SDL_AtomicLock)); }
__attribute__((naked)) void SDL_AtomicSet() { asm("jmp *%0" : : "m"(p_SDL_AtomicSet)); }
__attribute__((naked)) void SDL_AtomicSetPtr() { asm("jmp *%0" : : "m"(p_SDL_AtomicSetPtr)); }
__attribute__((naked)) void SDL_AtomicTryLock() { asm("jmp *%0" : : "m"(p_SDL_AtomicTryLock)); }
__attribute__((naked)) void SDL_AtomicUnlock() { asm("jmp *%0" : : "m"(p_SDL_AtomicUnlock)); }
__attribute__((naked)) void SDL_AudioInit() { asm("jmp *%0" : : "m"(p_SDL_AudioInit)); }
__attribute__((naked)) void SDL_AudioQuit() { asm("jmp *%0" : : "m"(p_SDL_AudioQuit)); }
__attribute__((naked)) void SDL_BuildAudioCVT() { asm("jmp *%0" : : "m"(p_SDL_BuildAudioCVT)); }
__attribute__((naked)) void SDL_CalculateGammaRamp() { asm("jmp *%0" : : "m"(p_SDL_CalculateGammaRamp)); }
__attribute__((naked)) void SDL_ClearError() { asm("jmp *%0" : : "m"(p_SDL_ClearError)); }
__attribute__((naked)) void SDL_ClearHints() { asm("jmp *%0" : : "m"(p_SDL_ClearHints)); }
__attribute__((naked)) void SDL_CloseAudio() { asm("jmp *%0" : : "m"(p_SDL_CloseAudio)); }
__attribute__((naked)) void SDL_CloseAudioDevice() { asm("jmp *%0" : : "m"(p_SDL_CloseAudioDevice)); }
__attribute__((naked)) void SDL_CondBroadcast() { asm("jmp *%0" : : "m"(p_SDL_CondBroadcast)); }
__attribute__((naked)) void SDL_CondSignal() { asm("jmp *%0" : : "m"(p_SDL_CondSignal)); }
__attribute__((naked)) void SDL_CondWait() { asm("jmp *%0" : : "m"(p_SDL_CondWait)); }
__attribute__((naked)) void SDL_CondWaitTimeout() { asm("jmp *%0" : : "m"(p_SDL_CondWaitTimeout)); }
__attribute__((naked)) void SDL_ConvertAudio() { asm("jmp *%0" : : "m"(p_SDL_ConvertAudio)); }
__attribute__((naked)) void SDL_ConvertPixels() { asm("jmp *%0" : : "m"(p_SDL_ConvertPixels)); }
__attribute__((naked)) void SDL_ConvertSurface() { asm("jmp *%0" : : "m"(p_SDL_ConvertSurface)); }
__attribute__((naked)) void SDL_ConvertSurfaceFormat() { asm("jmp *%0" : : "m"(p_SDL_ConvertSurfaceFormat)); }
__attribute__((naked)) void SDL_CreateColorCursor() { asm("jmp *%0" : : "m"(p_SDL_CreateColorCursor)); }
__attribute__((naked)) void SDL_CreateCond() { asm("jmp *%0" : : "m"(p_SDL_CreateCond)); }
__attribute__((naked)) void SDL_CreateCursor() { asm("jmp *%0" : : "m"(p_SDL_CreateCursor)); }
__attribute__((naked)) void SDL_CreateMutex() { asm("jmp *%0" : : "m"(p_SDL_CreateMutex)); }
__attribute__((naked)) void SDL_CreateRGBSurface() { asm("jmp *%0" : : "m"(p_SDL_CreateRGBSurface)); }
__attribute__((naked)) void SDL_CreateRGBSurfaceFrom() { asm("jmp *%0" : : "m"(p_SDL_CreateRGBSurfaceFrom)); }
__attribute__((naked)) void SDL_CreateRenderer() { asm("jmp *%0" : : "m"(p_SDL_CreateRenderer)); }
__attribute__((naked)) void SDL_CreateSemaphore() { asm("jmp *%0" : : "m"(p_SDL_CreateSemaphore)); }
__attribute__((naked)) void SDL_CreateShapedWindow() { asm("jmp *%0" : : "m"(p_SDL_CreateShapedWindow)); }
__attribute__((naked)) void SDL_CreateSoftwareRenderer() { asm("jmp *%0" : : "m"(p_SDL_CreateSoftwareRenderer)); }
__attribute__((naked)) void SDL_CreateSystemCursor() { asm("jmp *%0" : : "m"(p_SDL_CreateSystemCursor)); }
__attribute__((naked)) void SDL_CreateTexture() { asm("jmp *%0" : : "m"(p_SDL_CreateTexture)); }
__attribute__((naked)) void SDL_CreateTextureFromSurface() { asm("jmp *%0" : : "m"(p_SDL_CreateTextureFromSurface)); }
__attribute__((naked)) void SDL_CreateThread() { asm("jmp *%0" : : "m"(p_SDL_CreateThread)); }
/* Capture the game's main window when it's created. Fullscreen/window-size launch
 * args are applied via the game's OWN main_set_fullscreen/main_set_window (see
 * hooks.c) rather than raw SDL flags here - the game sets up its GL viewport for
 * its window, so a raw fullscreen flag just makes a huge window it renders a small
 * corner of. */
void* g_proxy_sdl_window = NULL;
/* A native main_set_window/main_set_fullscreen call asks SDL for display 0 and
 * recreates the window with SDL_WINDOWPOS_CENTERED_DISPLAY(0). The runtime sets
 * this one-shot override around those calls so F1/F11 stay on the monitor that
 * owned the old window. It remains -1 during normal SDL use. */
volatile LONG g_proxy_sdl_display_override = -1;

int __cdecl SDL_GetDisplayBounds(int display_index, void* rect) {
    typedef int (__cdecl *get_bounds_t)(int, void*);
    LONG override_index = InterlockedCompareExchange(&g_proxy_sdl_display_override, -1, -1);
    if (!p_SDL_GetDisplayBounds) return -1;
    if (display_index == 0 && override_index >= 0) {
        display_index = (int)override_index;
    }
    return ((get_bounds_t)p_SDL_GetDisplayBounds)(display_index, rect);
}

void* __cdecl SDL_CreateWindow(const char* title, int x, int y, int w, int h, unsigned int flags) {
    typedef void* (__cdecl *create_t)(const char*, int, int, int, int, unsigned int);
    LONG override_index = InterlockedCompareExchange(&g_proxy_sdl_display_override, -1, -1);
    if (override_index >= 0) {
        const unsigned int centered_mask = 0x2FFF0000u;
        if (((unsigned int)x & 0xFFFF0000u) == centered_mask) {
            x = (int)(centered_mask | ((unsigned int)override_index & 0xFFFFu));
        }
        if (((unsigned int)y & 0xFFFF0000u) == centered_mask) {
            y = (int)(centered_mask | ((unsigned int)override_index & 0xFFFFu));
        }
    }
    void* win = ((create_t)p_SDL_CreateWindow)(title, x, y, w, h, flags);
    g_proxy_sdl_window = win;
    return win;
}

void __cdecl SDL_DestroyWindow(void* window) {
    typedef void (__cdecl *destroy_t)(void*);
    (void)InterlockedCompareExchangePointer((PVOID volatile*)&g_proxy_sdl_window,
                                            NULL,
                                            window);
    ((destroy_t)p_SDL_DestroyWindow)(window);
}

__attribute__((naked)) void SDL_CreateWindowAndRenderer() { asm("jmp *%0" : : "m"(p_SDL_CreateWindowAndRenderer)); }
__attribute__((naked)) void SDL_CreateWindowFrom() { asm("jmp *%0" : : "m"(p_SDL_CreateWindowFrom)); }
__attribute__((naked)) void SDL_DXGIGetOutputInfo() { asm("jmp *%0" : : "m"(p_SDL_DXGIGetOutputInfo)); }
__attribute__((naked)) void SDL_DYNAPI_entry() { asm("jmp *%0" : : "m"(p_SDL_DYNAPI_entry)); }
__attribute__((naked)) void SDL_DelEventWatch() { asm("jmp *%0" : : "m"(p_SDL_DelEventWatch)); }
__attribute__((naked)) void SDL_DelHintCallback() { asm("jmp *%0" : : "m"(p_SDL_DelHintCallback)); }
__attribute__((naked)) void SDL_Delay() { asm("jmp *%0" : : "m"(p_SDL_Delay)); }
__attribute__((naked)) void SDL_DestroyCond() { asm("jmp *%0" : : "m"(p_SDL_DestroyCond)); }
__attribute__((naked)) void SDL_DestroyMutex() { asm("jmp *%0" : : "m"(p_SDL_DestroyMutex)); }
__attribute__((naked)) void SDL_DestroyRenderer() { asm("jmp *%0" : : "m"(p_SDL_DestroyRenderer)); }
__attribute__((naked)) void SDL_DestroySemaphore() { asm("jmp *%0" : : "m"(p_SDL_DestroySemaphore)); }
__attribute__((naked)) void SDL_DestroyTexture() { asm("jmp *%0" : : "m"(p_SDL_DestroyTexture)); }
__attribute__((naked)) void SDL_DetachThread() { asm("jmp *%0" : : "m"(p_SDL_DetachThread)); }
__attribute__((naked)) void SDL_Direct3D9GetAdapterIndex() { asm("jmp *%0" : : "m"(p_SDL_Direct3D9GetAdapterIndex)); }
__attribute__((naked)) void SDL_DisableScreenSaver() { asm("jmp *%0" : : "m"(p_SDL_DisableScreenSaver)); }
__attribute__((naked)) void SDL_EnableScreenSaver() { asm("jmp *%0" : : "m"(p_SDL_EnableScreenSaver)); }
__attribute__((naked)) void SDL_EnclosePoints() { asm("jmp *%0" : : "m"(p_SDL_EnclosePoints)); }
__attribute__((naked)) void SDL_Error() { asm("jmp *%0" : : "m"(p_SDL_Error)); }
__attribute__((naked)) void SDL_EventState() { asm("jmp *%0" : : "m"(p_SDL_EventState)); }
__attribute__((naked)) void SDL_FillRect() { asm("jmp *%0" : : "m"(p_SDL_FillRect)); }
__attribute__((naked)) void SDL_FillRects() { asm("jmp *%0" : : "m"(p_SDL_FillRects)); }
__attribute__((naked)) void SDL_FilterEvents() { asm("jmp *%0" : : "m"(p_SDL_FilterEvents)); }
__attribute__((naked)) void SDL_FlushEvent() { asm("jmp *%0" : : "m"(p_SDL_FlushEvent)); }
__attribute__((naked)) void SDL_FlushEvents() { asm("jmp *%0" : : "m"(p_SDL_FlushEvents)); }
__attribute__((naked)) void SDL_FreeCursor() { asm("jmp *%0" : : "m"(p_SDL_FreeCursor)); }
__attribute__((naked)) void SDL_FreeFormat() { asm("jmp *%0" : : "m"(p_SDL_FreeFormat)); }
__attribute__((naked)) void SDL_FreePalette() { asm("jmp *%0" : : "m"(p_SDL_FreePalette)); }
__attribute__((naked)) void SDL_FreeRW() { asm("jmp *%0" : : "m"(p_SDL_FreeRW)); }
__attribute__((naked)) void SDL_FreeSurface() { asm("jmp *%0" : : "m"(p_SDL_FreeSurface)); }
__attribute__((naked)) void SDL_FreeWAV() { asm("jmp *%0" : : "m"(p_SDL_FreeWAV)); }
__attribute__((naked)) void SDL_GL_BindTexture() { asm("jmp *%0" : : "m"(p_SDL_GL_BindTexture)); }
__attribute__((naked)) void SDL_GL_CreateContext() { asm("jmp *%0" : : "m"(p_SDL_GL_CreateContext)); }
__attribute__((naked)) void SDL_GL_DeleteContext() { asm("jmp *%0" : : "m"(p_SDL_GL_DeleteContext)); }
__attribute__((naked)) void SDL_GL_ExtensionSupported() { asm("jmp *%0" : : "m"(p_SDL_GL_ExtensionSupported)); }
__attribute__((naked)) void SDL_GL_GetAttribute() { asm("jmp *%0" : : "m"(p_SDL_GL_GetAttribute)); }
__attribute__((naked)) void SDL_GL_GetCurrentContext() { asm("jmp *%0" : : "m"(p_SDL_GL_GetCurrentContext)); }
__attribute__((naked)) void SDL_GL_GetCurrentWindow() { asm("jmp *%0" : : "m"(p_SDL_GL_GetCurrentWindow)); }
__attribute__((naked)) void SDL_GL_GetDrawableSize() { asm("jmp *%0" : : "m"(p_SDL_GL_GetDrawableSize)); }
__attribute__((naked)) void SDL_GL_GetProcAddress() { asm("jmp *%0" : : "m"(p_SDL_GL_GetProcAddress)); }
__attribute__((naked)) void SDL_GL_GetSwapInterval() { asm("jmp *%0" : : "m"(p_SDL_GL_GetSwapInterval)); }
__attribute__((naked)) void SDL_GL_LoadLibrary() { asm("jmp *%0" : : "m"(p_SDL_GL_LoadLibrary)); }
__attribute__((naked)) void SDL_GL_MakeCurrent() { asm("jmp *%0" : : "m"(p_SDL_GL_MakeCurrent)); }
__attribute__((naked)) void SDL_GL_ResetAttributes() { asm("jmp *%0" : : "m"(p_SDL_GL_ResetAttributes)); }
__attribute__((naked)) void SDL_GL_SetAttribute() { asm("jmp *%0" : : "m"(p_SDL_GL_SetAttribute)); }
__attribute__((naked)) void SDL_GL_SetSwapInterval() { asm("jmp *%0" : : "m"(p_SDL_GL_SetSwapInterval)); }
__attribute__((naked)) void SDL_GL_UnbindTexture() { asm("jmp *%0" : : "m"(p_SDL_GL_UnbindTexture)); }
__attribute__((naked)) void SDL_GL_UnloadLibrary() { asm("jmp *%0" : : "m"(p_SDL_GL_UnloadLibrary)); }
__attribute__((naked)) void SDL_GameControllerAddMapping() { asm("jmp *%0" : : "m"(p_SDL_GameControllerAddMapping)); }
__attribute__((naked)) void SDL_GameControllerAddMappingsFromRW() { asm("jmp *%0" : : "m"(p_SDL_GameControllerAddMappingsFromRW)); }
__attribute__((naked)) void SDL_GameControllerClose() { asm("jmp *%0" : : "m"(p_SDL_GameControllerClose)); }
__attribute__((naked)) void SDL_GameControllerEventState() { asm("jmp *%0" : : "m"(p_SDL_GameControllerEventState)); }
__attribute__((naked)) void SDL_GameControllerGetAttached() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetAttached)); }
__attribute__((naked)) void SDL_GameControllerGetAxis() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetAxis)); }
__attribute__((naked)) void SDL_GameControllerGetAxisFromString() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetAxisFromString)); }
__attribute__((naked)) void SDL_GameControllerGetBindForAxis() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetBindForAxis)); }
__attribute__((naked)) void SDL_GameControllerGetBindForButton() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetBindForButton)); }
__attribute__((naked)) void SDL_GameControllerGetButton() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetButton)); }
__attribute__((naked)) void SDL_GameControllerGetButtonFromString() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetButtonFromString)); }
__attribute__((naked)) void SDL_GameControllerGetJoystick() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetJoystick)); }
__attribute__((naked)) void SDL_GameControllerGetStringForAxis() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetStringForAxis)); }
__attribute__((naked)) void SDL_GameControllerGetStringForButton() { asm("jmp *%0" : : "m"(p_SDL_GameControllerGetStringForButton)); }
__attribute__((naked)) void SDL_GameControllerMapping() { asm("jmp *%0" : : "m"(p_SDL_GameControllerMapping)); }
__attribute__((naked)) void SDL_GameControllerMappingForGUID() { asm("jmp *%0" : : "m"(p_SDL_GameControllerMappingForGUID)); }
__attribute__((naked)) void SDL_GameControllerName() { asm("jmp *%0" : : "m"(p_SDL_GameControllerName)); }
__attribute__((naked)) void SDL_GameControllerNameForIndex() { asm("jmp *%0" : : "m"(p_SDL_GameControllerNameForIndex)); }
__attribute__((naked)) void SDL_GameControllerOpen() { asm("jmp *%0" : : "m"(p_SDL_GameControllerOpen)); }
__attribute__((naked)) void SDL_GameControllerUpdate() { asm("jmp *%0" : : "m"(p_SDL_GameControllerUpdate)); }
__attribute__((naked)) void SDL_GetAssertionHandler() { asm("jmp *%0" : : "m"(p_SDL_GetAssertionHandler)); }
__attribute__((naked)) void SDL_GetAssertionReport() { asm("jmp *%0" : : "m"(p_SDL_GetAssertionReport)); }
__attribute__((naked)) void SDL_GetAudioDeviceName() { asm("jmp *%0" : : "m"(p_SDL_GetAudioDeviceName)); }
__attribute__((naked)) void SDL_GetAudioDeviceStatus() { asm("jmp *%0" : : "m"(p_SDL_GetAudioDeviceStatus)); }
__attribute__((naked)) void SDL_GetAudioDriver() { asm("jmp *%0" : : "m"(p_SDL_GetAudioDriver)); }
__attribute__((naked)) void SDL_GetAudioStatus() { asm("jmp *%0" : : "m"(p_SDL_GetAudioStatus)); }
__attribute__((naked)) void SDL_GetBasePath() { asm("jmp *%0" : : "m"(p_SDL_GetBasePath)); }
__attribute__((naked)) void SDL_GetCPUCacheLineSize() { asm("jmp *%0" : : "m"(p_SDL_GetCPUCacheLineSize)); }
__attribute__((naked)) void SDL_GetCPUCount() { asm("jmp *%0" : : "m"(p_SDL_GetCPUCount)); }
__attribute__((naked)) void SDL_GetClipRect() { asm("jmp *%0" : : "m"(p_SDL_GetClipRect)); }
__attribute__((naked)) void SDL_GetClipboardText() { asm("jmp *%0" : : "m"(p_SDL_GetClipboardText)); }
__attribute__((naked)) void SDL_GetClosestDisplayMode() { asm("jmp *%0" : : "m"(p_SDL_GetClosestDisplayMode)); }
__attribute__((naked)) void SDL_GetColorKey() { asm("jmp *%0" : : "m"(p_SDL_GetColorKey)); }
__attribute__((naked)) void SDL_GetCurrentAudioDriver() { asm("jmp *%0" : : "m"(p_SDL_GetCurrentAudioDriver)); }
__attribute__((naked)) void SDL_GetCurrentDisplayMode() { asm("jmp *%0" : : "m"(p_SDL_GetCurrentDisplayMode)); }
__attribute__((naked)) void SDL_GetCurrentVideoDriver() { asm("jmp *%0" : : "m"(p_SDL_GetCurrentVideoDriver)); }
__attribute__((naked)) void SDL_GetCursor() { asm("jmp *%0" : : "m"(p_SDL_GetCursor)); }
__attribute__((naked)) void SDL_GetDefaultAssertionHandler() { asm("jmp *%0" : : "m"(p_SDL_GetDefaultAssertionHandler)); }
__attribute__((naked)) void SDL_GetDefaultCursor() { asm("jmp *%0" : : "m"(p_SDL_GetDefaultCursor)); }
__attribute__((naked)) void SDL_GetDesktopDisplayMode() { asm("jmp *%0" : : "m"(p_SDL_GetDesktopDisplayMode)); }
__attribute__((naked)) void SDL_GetDisplayMode() { asm("jmp *%0" : : "m"(p_SDL_GetDisplayMode)); }
__attribute__((naked)) void SDL_GetDisplayName() { asm("jmp *%0" : : "m"(p_SDL_GetDisplayName)); }
__attribute__((naked)) void SDL_GetError() { asm("jmp *%0" : : "m"(p_SDL_GetError)); }
__attribute__((naked)) void SDL_GetEventFilter() { asm("jmp *%0" : : "m"(p_SDL_GetEventFilter)); }
__attribute__((naked)) void SDL_GetHint() { asm("jmp *%0" : : "m"(p_SDL_GetHint)); }
__attribute__((naked)) void SDL_GetKeyFromName() { asm("jmp *%0" : : "m"(p_SDL_GetKeyFromName)); }
__attribute__((naked)) void SDL_GetKeyFromScancode() { asm("jmp *%0" : : "m"(p_SDL_GetKeyFromScancode)); }
__attribute__((naked)) void SDL_GetKeyName() { asm("jmp *%0" : : "m"(p_SDL_GetKeyName)); }
__attribute__((naked)) void SDL_GetKeyboardFocus() { asm("jmp *%0" : : "m"(p_SDL_GetKeyboardFocus)); }
__attribute__((naked)) void SDL_GetKeyboardState() { asm("jmp *%0" : : "m"(p_SDL_GetKeyboardState)); }
__attribute__((naked)) void SDL_GetModState() { asm("jmp *%0" : : "m"(p_SDL_GetModState)); }
__attribute__((naked)) void SDL_GetMouseFocus() { asm("jmp *%0" : : "m"(p_SDL_GetMouseFocus)); }
__attribute__((naked)) void SDL_GetMouseState() { asm("jmp *%0" : : "m"(p_SDL_GetMouseState)); }
__attribute__((naked)) void SDL_GetNumAudioDevices() { asm("jmp *%0" : : "m"(p_SDL_GetNumAudioDevices)); }
__attribute__((naked)) void SDL_GetNumAudioDrivers() { asm("jmp *%0" : : "m"(p_SDL_GetNumAudioDrivers)); }
__attribute__((naked)) void SDL_GetNumDisplayModes() { asm("jmp *%0" : : "m"(p_SDL_GetNumDisplayModes)); }
__attribute__((naked)) void SDL_GetNumRenderDrivers() { asm("jmp *%0" : : "m"(p_SDL_GetNumRenderDrivers)); }
__attribute__((naked)) void SDL_GetNumTouchDevices() { asm("jmp *%0" : : "m"(p_SDL_GetNumTouchDevices)); }
__attribute__((naked)) void SDL_GetNumTouchFingers() { asm("jmp *%0" : : "m"(p_SDL_GetNumTouchFingers)); }
__attribute__((naked)) void SDL_GetNumVideoDisplays() { asm("jmp *%0" : : "m"(p_SDL_GetNumVideoDisplays)); }
__attribute__((naked)) void SDL_GetNumVideoDrivers() { asm("jmp *%0" : : "m"(p_SDL_GetNumVideoDrivers)); }
__attribute__((naked)) void SDL_GetPerformanceCounter() { asm("jmp *%0" : : "m"(p_SDL_GetPerformanceCounter)); }
__attribute__((naked)) void SDL_GetPerformanceFrequency() { asm("jmp *%0" : : "m"(p_SDL_GetPerformanceFrequency)); }
__attribute__((naked)) void SDL_GetPixelFormatName() { asm("jmp *%0" : : "m"(p_SDL_GetPixelFormatName)); }
__attribute__((naked)) void SDL_GetPlatform() { asm("jmp *%0" : : "m"(p_SDL_GetPlatform)); }
__attribute__((naked)) void SDL_GetPowerInfo() { asm("jmp *%0" : : "m"(p_SDL_GetPowerInfo)); }
__attribute__((naked)) void SDL_GetPrefPath() { asm("jmp *%0" : : "m"(p_SDL_GetPrefPath)); }
__attribute__((naked)) void SDL_GetRGB() { asm("jmp *%0" : : "m"(p_SDL_GetRGB)); }
__attribute__((naked)) void SDL_GetRGBA() { asm("jmp *%0" : : "m"(p_SDL_GetRGBA)); }
__attribute__((naked)) void SDL_GetRelativeMouseMode() { asm("jmp *%0" : : "m"(p_SDL_GetRelativeMouseMode)); }
__attribute__((naked)) void SDL_GetRelativeMouseState() { asm("jmp *%0" : : "m"(p_SDL_GetRelativeMouseState)); }
__attribute__((naked)) void SDL_GetRenderDrawBlendMode() { asm("jmp *%0" : : "m"(p_SDL_GetRenderDrawBlendMode)); }
__attribute__((naked)) void SDL_GetRenderDrawColor() { asm("jmp *%0" : : "m"(p_SDL_GetRenderDrawColor)); }
__attribute__((naked)) void SDL_GetRenderDriverInfo() { asm("jmp *%0" : : "m"(p_SDL_GetRenderDriverInfo)); }
__attribute__((naked)) void SDL_GetRenderTarget() { asm("jmp *%0" : : "m"(p_SDL_GetRenderTarget)); }
__attribute__((naked)) void SDL_GetRenderer() { asm("jmp *%0" : : "m"(p_SDL_GetRenderer)); }
__attribute__((naked)) void SDL_GetRendererInfo() { asm("jmp *%0" : : "m"(p_SDL_GetRendererInfo)); }
__attribute__((naked)) void SDL_GetRendererOutputSize() { asm("jmp *%0" : : "m"(p_SDL_GetRendererOutputSize)); }
__attribute__((naked)) void SDL_GetRevision() { asm("jmp *%0" : : "m"(p_SDL_GetRevision)); }
__attribute__((naked)) void SDL_GetRevisionNumber() { asm("jmp *%0" : : "m"(p_SDL_GetRevisionNumber)); }
__attribute__((naked)) void SDL_GetScancodeFromKey() { asm("jmp *%0" : : "m"(p_SDL_GetScancodeFromKey)); }
__attribute__((naked)) void SDL_GetScancodeFromName() { asm("jmp *%0" : : "m"(p_SDL_GetScancodeFromName)); }
__attribute__((naked)) void SDL_GetScancodeName() { asm("jmp *%0" : : "m"(p_SDL_GetScancodeName)); }
__attribute__((naked)) void SDL_GetShapedWindowMode() { asm("jmp *%0" : : "m"(p_SDL_GetShapedWindowMode)); }
__attribute__((naked)) void SDL_GetSurfaceAlphaMod() { asm("jmp *%0" : : "m"(p_SDL_GetSurfaceAlphaMod)); }
__attribute__((naked)) void SDL_GetSurfaceBlendMode() { asm("jmp *%0" : : "m"(p_SDL_GetSurfaceBlendMode)); }
__attribute__((naked)) void SDL_GetSurfaceColorMod() { asm("jmp *%0" : : "m"(p_SDL_GetSurfaceColorMod)); }
__attribute__((naked)) void SDL_GetSystemRAM() { asm("jmp *%0" : : "m"(p_SDL_GetSystemRAM)); }
__attribute__((naked)) void SDL_GetTextureAlphaMod() { asm("jmp *%0" : : "m"(p_SDL_GetTextureAlphaMod)); }
__attribute__((naked)) void SDL_GetTextureBlendMode() { asm("jmp *%0" : : "m"(p_SDL_GetTextureBlendMode)); }
__attribute__((naked)) void SDL_GetTextureColorMod() { asm("jmp *%0" : : "m"(p_SDL_GetTextureColorMod)); }
__attribute__((naked)) void SDL_GetThreadID() { asm("jmp *%0" : : "m"(p_SDL_GetThreadID)); }
__attribute__((naked)) void SDL_GetThreadName() { asm("jmp *%0" : : "m"(p_SDL_GetThreadName)); }
__attribute__((naked)) void SDL_GetTicks() { asm("jmp *%0" : : "m"(p_SDL_GetTicks)); }
__attribute__((naked)) void SDL_GetTouchDevice() { asm("jmp *%0" : : "m"(p_SDL_GetTouchDevice)); }
__attribute__((naked)) void SDL_GetTouchFinger() { asm("jmp *%0" : : "m"(p_SDL_GetTouchFinger)); }
__attribute__((naked)) void SDL_GetVersion() { asm("jmp *%0" : : "m"(p_SDL_GetVersion)); }
__attribute__((naked)) void SDL_GetVideoDriver() { asm("jmp *%0" : : "m"(p_SDL_GetVideoDriver)); }
__attribute__((naked)) void SDL_GetWindowBrightness() { asm("jmp *%0" : : "m"(p_SDL_GetWindowBrightness)); }
__attribute__((naked)) void SDL_GetWindowData() { asm("jmp *%0" : : "m"(p_SDL_GetWindowData)); }
__attribute__((naked)) void SDL_GetWindowDisplayIndex() { asm("jmp *%0" : : "m"(p_SDL_GetWindowDisplayIndex)); }
__attribute__((naked)) void SDL_GetWindowDisplayMode() { asm("jmp *%0" : : "m"(p_SDL_GetWindowDisplayMode)); }
__attribute__((naked)) void SDL_GetWindowFlags() { asm("jmp *%0" : : "m"(p_SDL_GetWindowFlags)); }
__attribute__((naked)) void SDL_GetWindowFromID() { asm("jmp *%0" : : "m"(p_SDL_GetWindowFromID)); }
__attribute__((naked)) void SDL_GetWindowGammaRamp() { asm("jmp *%0" : : "m"(p_SDL_GetWindowGammaRamp)); }
__attribute__((naked)) void SDL_GetWindowGrab() { asm("jmp *%0" : : "m"(p_SDL_GetWindowGrab)); }
__attribute__((naked)) void SDL_GetWindowID() { asm("jmp *%0" : : "m"(p_SDL_GetWindowID)); }
__attribute__((naked)) void SDL_GetWindowMaximumSize() { asm("jmp *%0" : : "m"(p_SDL_GetWindowMaximumSize)); }
__attribute__((naked)) void SDL_GetWindowMinimumSize() { asm("jmp *%0" : : "m"(p_SDL_GetWindowMinimumSize)); }
__attribute__((naked)) void SDL_GetWindowPixelFormat() { asm("jmp *%0" : : "m"(p_SDL_GetWindowPixelFormat)); }
__attribute__((naked)) void SDL_GetWindowPosition() { asm("jmp *%0" : : "m"(p_SDL_GetWindowPosition)); }
__attribute__((naked)) void SDL_GetWindowSize() { asm("jmp *%0" : : "m"(p_SDL_GetWindowSize)); }
__attribute__((naked)) void SDL_GetWindowSurface() { asm("jmp *%0" : : "m"(p_SDL_GetWindowSurface)); }
__attribute__((naked)) void SDL_GetWindowTitle() { asm("jmp *%0" : : "m"(p_SDL_GetWindowTitle)); }
__attribute__((naked)) void SDL_GetWindowWMInfo() { asm("jmp *%0" : : "m"(p_SDL_GetWindowWMInfo)); }
__attribute__((naked)) void SDL_HapticClose() { asm("jmp *%0" : : "m"(p_SDL_HapticClose)); }
__attribute__((naked)) void SDL_HapticDestroyEffect() { asm("jmp *%0" : : "m"(p_SDL_HapticDestroyEffect)); }
__attribute__((naked)) void SDL_HapticEffectSupported() { asm("jmp *%0" : : "m"(p_SDL_HapticEffectSupported)); }
__attribute__((naked)) void SDL_HapticGetEffectStatus() { asm("jmp *%0" : : "m"(p_SDL_HapticGetEffectStatus)); }
__attribute__((naked)) void SDL_HapticIndex() { asm("jmp *%0" : : "m"(p_SDL_HapticIndex)); }
__attribute__((naked)) void SDL_HapticName() { asm("jmp *%0" : : "m"(p_SDL_HapticName)); }
__attribute__((naked)) void SDL_HapticNewEffect() { asm("jmp *%0" : : "m"(p_SDL_HapticNewEffect)); }
__attribute__((naked)) void SDL_HapticNumAxes() { asm("jmp *%0" : : "m"(p_SDL_HapticNumAxes)); }
__attribute__((naked)) void SDL_HapticNumEffects() { asm("jmp *%0" : : "m"(p_SDL_HapticNumEffects)); }
__attribute__((naked)) void SDL_HapticNumEffectsPlaying() { asm("jmp *%0" : : "m"(p_SDL_HapticNumEffectsPlaying)); }
__attribute__((naked)) void SDL_HapticOpen() { asm("jmp *%0" : : "m"(p_SDL_HapticOpen)); }
__attribute__((naked)) void SDL_HapticOpenFromJoystick() { asm("jmp *%0" : : "m"(p_SDL_HapticOpenFromJoystick)); }
__attribute__((naked)) void SDL_HapticOpenFromMouse() { asm("jmp *%0" : : "m"(p_SDL_HapticOpenFromMouse)); }
__attribute__((naked)) void SDL_HapticOpened() { asm("jmp *%0" : : "m"(p_SDL_HapticOpened)); }
__attribute__((naked)) void SDL_HapticPause() { asm("jmp *%0" : : "m"(p_SDL_HapticPause)); }
__attribute__((naked)) void SDL_HapticQuery() { asm("jmp *%0" : : "m"(p_SDL_HapticQuery)); }
__attribute__((naked)) void SDL_HapticRumbleInit() { asm("jmp *%0" : : "m"(p_SDL_HapticRumbleInit)); }
__attribute__((naked)) void SDL_HapticRumblePlay() { asm("jmp *%0" : : "m"(p_SDL_HapticRumblePlay)); }
__attribute__((naked)) void SDL_HapticRumbleStop() { asm("jmp *%0" : : "m"(p_SDL_HapticRumbleStop)); }
__attribute__((naked)) void SDL_HapticRumbleSupported() { asm("jmp *%0" : : "m"(p_SDL_HapticRumbleSupported)); }
__attribute__((naked)) void SDL_HapticRunEffect() { asm("jmp *%0" : : "m"(p_SDL_HapticRunEffect)); }
__attribute__((naked)) void SDL_HapticSetAutocenter() { asm("jmp *%0" : : "m"(p_SDL_HapticSetAutocenter)); }
__attribute__((naked)) void SDL_HapticSetGain() { asm("jmp *%0" : : "m"(p_SDL_HapticSetGain)); }
__attribute__((naked)) void SDL_HapticStopAll() { asm("jmp *%0" : : "m"(p_SDL_HapticStopAll)); }
__attribute__((naked)) void SDL_HapticStopEffect() { asm("jmp *%0" : : "m"(p_SDL_HapticStopEffect)); }
__attribute__((naked)) void SDL_HapticUnpause() { asm("jmp *%0" : : "m"(p_SDL_HapticUnpause)); }
__attribute__((naked)) void SDL_HapticUpdateEffect() { asm("jmp *%0" : : "m"(p_SDL_HapticUpdateEffect)); }
__attribute__((naked)) void SDL_Has3DNow() { asm("jmp *%0" : : "m"(p_SDL_Has3DNow)); }
__attribute__((naked)) void SDL_HasAVX() { asm("jmp *%0" : : "m"(p_SDL_HasAVX)); }
__attribute__((naked)) void SDL_HasAltiVec() { asm("jmp *%0" : : "m"(p_SDL_HasAltiVec)); }
__attribute__((naked)) void SDL_HasClipboardText() { asm("jmp *%0" : : "m"(p_SDL_HasClipboardText)); }
__attribute__((naked)) void SDL_HasEvent() { asm("jmp *%0" : : "m"(p_SDL_HasEvent)); }
__attribute__((naked)) void SDL_HasEvents() { asm("jmp *%0" : : "m"(p_SDL_HasEvents)); }
__attribute__((naked)) void SDL_HasIntersection() { asm("jmp *%0" : : "m"(p_SDL_HasIntersection)); }
__attribute__((naked)) void SDL_HasMMX() { asm("jmp *%0" : : "m"(p_SDL_HasMMX)); }
__attribute__((naked)) void SDL_HasRDTSC() { asm("jmp *%0" : : "m"(p_SDL_HasRDTSC)); }
__attribute__((naked)) void SDL_HasSSE() { asm("jmp *%0" : : "m"(p_SDL_HasSSE)); }
__attribute__((naked)) void SDL_HasSSE2() { asm("jmp *%0" : : "m"(p_SDL_HasSSE2)); }
__attribute__((naked)) void SDL_HasSSE3() { asm("jmp *%0" : : "m"(p_SDL_HasSSE3)); }
__attribute__((naked)) void SDL_HasSSE41() { asm("jmp *%0" : : "m"(p_SDL_HasSSE41)); }
__attribute__((naked)) void SDL_HasSSE42() { asm("jmp *%0" : : "m"(p_SDL_HasSSE42)); }
__attribute__((naked)) void SDL_HasScreenKeyboardSupport() { asm("jmp *%0" : : "m"(p_SDL_HasScreenKeyboardSupport)); }
__attribute__((naked)) void SDL_HideWindow() { asm("jmp *%0" : : "m"(p_SDL_HideWindow)); }
__attribute__((naked)) void SDL_Init() { asm("jmp *%0" : : "m"(p_SDL_Init)); }
__attribute__((naked)) void SDL_InitSubSystem() { asm("jmp *%0" : : "m"(p_SDL_InitSubSystem)); }
__attribute__((naked)) void SDL_IntersectRect() { asm("jmp *%0" : : "m"(p_SDL_IntersectRect)); }
__attribute__((naked)) void SDL_IntersectRectAndLine() { asm("jmp *%0" : : "m"(p_SDL_IntersectRectAndLine)); }
__attribute__((naked)) void SDL_IsGameController() { asm("jmp *%0" : : "m"(p_SDL_IsGameController)); }
__attribute__((naked)) void SDL_IsScreenKeyboardShown() { asm("jmp *%0" : : "m"(p_SDL_IsScreenKeyboardShown)); }
__attribute__((naked)) void SDL_IsScreenSaverEnabled() { asm("jmp *%0" : : "m"(p_SDL_IsScreenSaverEnabled)); }
__attribute__((naked)) void SDL_IsShapedWindow() { asm("jmp *%0" : : "m"(p_SDL_IsShapedWindow)); }
__attribute__((naked)) void SDL_IsTextInputActive() { asm("jmp *%0" : : "m"(p_SDL_IsTextInputActive)); }
__attribute__((naked)) void SDL_JoystickClose() { asm("jmp *%0" : : "m"(p_SDL_JoystickClose)); }
__attribute__((naked)) void SDL_JoystickEventState() { asm("jmp *%0" : : "m"(p_SDL_JoystickEventState)); }
__attribute__((naked)) void SDL_JoystickGetAttached() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetAttached)); }
__attribute__((naked)) void SDL_JoystickGetAxis() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetAxis)); }
__attribute__((naked)) void SDL_JoystickGetBall() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetBall)); }
__attribute__((naked)) void SDL_JoystickGetButton() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetButton)); }
__attribute__((naked)) void SDL_JoystickGetDeviceGUID() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetDeviceGUID)); }
__attribute__((naked)) void SDL_JoystickGetGUID() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetGUID)); }
__attribute__((naked)) void SDL_JoystickGetGUIDFromString() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetGUIDFromString)); }
__attribute__((naked)) void SDL_JoystickGetGUIDString() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetGUIDString)); }
__attribute__((naked)) void SDL_JoystickGetHat() { asm("jmp *%0" : : "m"(p_SDL_JoystickGetHat)); }
__attribute__((naked)) void SDL_JoystickInstanceID() { asm("jmp *%0" : : "m"(p_SDL_JoystickInstanceID)); }
__attribute__((naked)) void SDL_JoystickIsHaptic() { asm("jmp *%0" : : "m"(p_SDL_JoystickIsHaptic)); }
__attribute__((naked)) void SDL_JoystickName() { asm("jmp *%0" : : "m"(p_SDL_JoystickName)); }
__attribute__((naked)) void SDL_JoystickNameForIndex() { asm("jmp *%0" : : "m"(p_SDL_JoystickNameForIndex)); }
__attribute__((naked)) void SDL_JoystickNumAxes() { asm("jmp *%0" : : "m"(p_SDL_JoystickNumAxes)); }
__attribute__((naked)) void SDL_JoystickNumBalls() { asm("jmp *%0" : : "m"(p_SDL_JoystickNumBalls)); }
__attribute__((naked)) void SDL_JoystickNumButtons() { asm("jmp *%0" : : "m"(p_SDL_JoystickNumButtons)); }
__attribute__((naked)) void SDL_JoystickNumHats() { asm("jmp *%0" : : "m"(p_SDL_JoystickNumHats)); }
__attribute__((naked)) void SDL_JoystickOpen() { asm("jmp *%0" : : "m"(p_SDL_JoystickOpen)); }
__attribute__((naked)) void SDL_JoystickUpdate() { asm("jmp *%0" : : "m"(p_SDL_JoystickUpdate)); }
__attribute__((naked)) void SDL_LoadBMP_RW() { asm("jmp *%0" : : "m"(p_SDL_LoadBMP_RW)); }
__attribute__((naked)) void SDL_LoadDollarTemplates() { asm("jmp *%0" : : "m"(p_SDL_LoadDollarTemplates)); }
__attribute__((naked)) void SDL_LoadFunction() { asm("jmp *%0" : : "m"(p_SDL_LoadFunction)); }
__attribute__((naked)) void SDL_LoadObject() { asm("jmp *%0" : : "m"(p_SDL_LoadObject)); }
__attribute__((naked)) void SDL_LoadWAV_RW() { asm("jmp *%0" : : "m"(p_SDL_LoadWAV_RW)); }
__attribute__((naked)) void SDL_LockAudio() { asm("jmp *%0" : : "m"(p_SDL_LockAudio)); }
__attribute__((naked)) void SDL_LockAudioDevice() { asm("jmp *%0" : : "m"(p_SDL_LockAudioDevice)); }
__attribute__((naked)) void SDL_LockMutex() { asm("jmp *%0" : : "m"(p_SDL_LockMutex)); }
__attribute__((naked)) void SDL_LockSurface() { asm("jmp *%0" : : "m"(p_SDL_LockSurface)); }
__attribute__((naked)) void SDL_LockTexture() { asm("jmp *%0" : : "m"(p_SDL_LockTexture)); }
__attribute__((naked)) void SDL_Log() { asm("jmp *%0" : : "m"(p_SDL_Log)); }
__attribute__((naked)) void SDL_LogCritical() { asm("jmp *%0" : : "m"(p_SDL_LogCritical)); }
__attribute__((naked)) void SDL_LogDebug() { asm("jmp *%0" : : "m"(p_SDL_LogDebug)); }
__attribute__((naked)) void SDL_LogError() { asm("jmp *%0" : : "m"(p_SDL_LogError)); }
__attribute__((naked)) void SDL_LogGetOutputFunction() { asm("jmp *%0" : : "m"(p_SDL_LogGetOutputFunction)); }
__attribute__((naked)) void SDL_LogGetPriority() { asm("jmp *%0" : : "m"(p_SDL_LogGetPriority)); }
__attribute__((naked)) void SDL_LogInfo() { asm("jmp *%0" : : "m"(p_SDL_LogInfo)); }
__attribute__((naked)) void SDL_LogMessage() { asm("jmp *%0" : : "m"(p_SDL_LogMessage)); }
__attribute__((naked)) void SDL_LogMessageV() { asm("jmp *%0" : : "m"(p_SDL_LogMessageV)); }
__attribute__((naked)) void SDL_LogResetPriorities() { asm("jmp *%0" : : "m"(p_SDL_LogResetPriorities)); }
__attribute__((naked)) void SDL_LogSetAllPriority() { asm("jmp *%0" : : "m"(p_SDL_LogSetAllPriority)); }
__attribute__((naked)) void SDL_LogSetOutputFunction() { asm("jmp *%0" : : "m"(p_SDL_LogSetOutputFunction)); }
__attribute__((naked)) void SDL_LogSetPriority() { asm("jmp *%0" : : "m"(p_SDL_LogSetPriority)); }
__attribute__((naked)) void SDL_LogVerbose() { asm("jmp *%0" : : "m"(p_SDL_LogVerbose)); }
__attribute__((naked)) void SDL_LogWarn() { asm("jmp *%0" : : "m"(p_SDL_LogWarn)); }
__attribute__((naked)) void SDL_LowerBlit() { asm("jmp *%0" : : "m"(p_SDL_LowerBlit)); }
__attribute__((naked)) void SDL_LowerBlitScaled() { asm("jmp *%0" : : "m"(p_SDL_LowerBlitScaled)); }
__attribute__((naked)) void SDL_MapRGB() { asm("jmp *%0" : : "m"(p_SDL_MapRGB)); }
__attribute__((naked)) void SDL_MapRGBA() { asm("jmp *%0" : : "m"(p_SDL_MapRGBA)); }
__attribute__((naked)) void SDL_MasksToPixelFormatEnum() { asm("jmp *%0" : : "m"(p_SDL_MasksToPixelFormatEnum)); }
__attribute__((naked)) void SDL_MaximizeWindow() { asm("jmp *%0" : : "m"(p_SDL_MaximizeWindow)); }
__attribute__((naked)) void SDL_MinimizeWindow() { asm("jmp *%0" : : "m"(p_SDL_MinimizeWindow)); }
__attribute__((naked)) void SDL_MixAudio() { asm("jmp *%0" : : "m"(p_SDL_MixAudio)); }
__attribute__((naked)) void SDL_MixAudioFormat() { asm("jmp *%0" : : "m"(p_SDL_MixAudioFormat)); }
__attribute__((naked)) void SDL_MouseIsHaptic() { asm("jmp *%0" : : "m"(p_SDL_MouseIsHaptic)); }
__attribute__((naked)) void SDL_NumHaptics() { asm("jmp *%0" : : "m"(p_SDL_NumHaptics)); }
__attribute__((naked)) void SDL_NumJoysticks() { asm("jmp *%0" : : "m"(p_SDL_NumJoysticks)); }
__attribute__((naked)) void SDL_OpenAudio() { asm("jmp *%0" : : "m"(p_SDL_OpenAudio)); }
__attribute__((naked)) void SDL_OpenAudioDevice() { asm("jmp *%0" : : "m"(p_SDL_OpenAudioDevice)); }
__attribute__((naked)) void SDL_PauseAudio() { asm("jmp *%0" : : "m"(p_SDL_PauseAudio)); }
__attribute__((naked)) void SDL_PauseAudioDevice() { asm("jmp *%0" : : "m"(p_SDL_PauseAudioDevice)); }
__attribute__((naked)) void SDL_PeepEvents() { asm("jmp *%0" : : "m"(p_SDL_PeepEvents)); }
__attribute__((naked)) void SDL_PixelFormatEnumToMasks() { asm("jmp *%0" : : "m"(p_SDL_PixelFormatEnumToMasks)); }
__attribute__((naked)) void SDL_PumpEvents() { asm("jmp *%0" : : "m"(p_SDL_PumpEvents)); }
__attribute__((naked)) void SDL_PushEvent() { asm("jmp *%0" : : "m"(p_SDL_PushEvent)); }
__attribute__((naked)) void SDL_QueryTexture() { asm("jmp *%0" : : "m"(p_SDL_QueryTexture)); }
__attribute__((naked)) void SDL_Quit() { asm("jmp *%0" : : "m"(p_SDL_Quit)); }
__attribute__((naked)) void SDL_QuitSubSystem() { asm("jmp *%0" : : "m"(p_SDL_QuitSubSystem)); }
__attribute__((naked)) void SDL_RWFromConstMem() { asm("jmp *%0" : : "m"(p_SDL_RWFromConstMem)); }
__attribute__((naked)) void SDL_RWFromFP() { asm("jmp *%0" : : "m"(p_SDL_RWFromFP)); }
__attribute__((naked)) void SDL_RWFromFile() { asm("jmp *%0" : : "m"(p_SDL_RWFromFile)); }
__attribute__((naked)) void SDL_RWFromMem() { asm("jmp *%0" : : "m"(p_SDL_RWFromMem)); }
__attribute__((naked)) void SDL_RaiseWindow() { asm("jmp *%0" : : "m"(p_SDL_RaiseWindow)); }
__attribute__((naked)) void SDL_ReadBE16() { asm("jmp *%0" : : "m"(p_SDL_ReadBE16)); }
__attribute__((naked)) void SDL_ReadBE32() { asm("jmp *%0" : : "m"(p_SDL_ReadBE32)); }
__attribute__((naked)) void SDL_ReadBE64() { asm("jmp *%0" : : "m"(p_SDL_ReadBE64)); }
__attribute__((naked)) void SDL_ReadLE16() { asm("jmp *%0" : : "m"(p_SDL_ReadLE16)); }
__attribute__((naked)) void SDL_ReadLE32() { asm("jmp *%0" : : "m"(p_SDL_ReadLE32)); }
__attribute__((naked)) void SDL_ReadLE64() { asm("jmp *%0" : : "m"(p_SDL_ReadLE64)); }
__attribute__((naked)) void SDL_ReadU8() { asm("jmp *%0" : : "m"(p_SDL_ReadU8)); }
__attribute__((naked)) void SDL_RecordGesture() { asm("jmp *%0" : : "m"(p_SDL_RecordGesture)); }
__attribute__((naked)) void SDL_RegisterApp() { asm("jmp *%0" : : "m"(p_SDL_RegisterApp)); }
__attribute__((naked)) void SDL_RegisterEvents() { asm("jmp *%0" : : "m"(p_SDL_RegisterEvents)); }
__attribute__((naked)) void SDL_RemoveTimer() { asm("jmp *%0" : : "m"(p_SDL_RemoveTimer)); }
__attribute__((naked)) void SDL_RenderClear() { asm("jmp *%0" : : "m"(p_SDL_RenderClear)); }
__attribute__((naked)) void SDL_RenderCopy() { asm("jmp *%0" : : "m"(p_SDL_RenderCopy)); }
__attribute__((naked)) void SDL_RenderCopyEx() { asm("jmp *%0" : : "m"(p_SDL_RenderCopyEx)); }
__attribute__((naked)) void SDL_RenderDrawLine() { asm("jmp *%0" : : "m"(p_SDL_RenderDrawLine)); }
__attribute__((naked)) void SDL_RenderDrawLines() { asm("jmp *%0" : : "m"(p_SDL_RenderDrawLines)); }
__attribute__((naked)) void SDL_RenderDrawPoint() { asm("jmp *%0" : : "m"(p_SDL_RenderDrawPoint)); }
__attribute__((naked)) void SDL_RenderDrawPoints() { asm("jmp *%0" : : "m"(p_SDL_RenderDrawPoints)); }
__attribute__((naked)) void SDL_RenderDrawRect() { asm("jmp *%0" : : "m"(p_SDL_RenderDrawRect)); }
__attribute__((naked)) void SDL_RenderDrawRects() { asm("jmp *%0" : : "m"(p_SDL_RenderDrawRects)); }
__attribute__((naked)) void SDL_RenderFillRect() { asm("jmp *%0" : : "m"(p_SDL_RenderFillRect)); }
__attribute__((naked)) void SDL_RenderFillRects() { asm("jmp *%0" : : "m"(p_SDL_RenderFillRects)); }
__attribute__((naked)) void SDL_RenderGetClipRect() { asm("jmp *%0" : : "m"(p_SDL_RenderGetClipRect)); }
__attribute__((naked)) void SDL_RenderGetD3D9Device() { asm("jmp *%0" : : "m"(p_SDL_RenderGetD3D9Device)); }
__attribute__((naked)) void SDL_RenderGetLogicalSize() { asm("jmp *%0" : : "m"(p_SDL_RenderGetLogicalSize)); }
__attribute__((naked)) void SDL_RenderGetScale() { asm("jmp *%0" : : "m"(p_SDL_RenderGetScale)); }
__attribute__((naked)) void SDL_RenderGetViewport() { asm("jmp *%0" : : "m"(p_SDL_RenderGetViewport)); }
__attribute__((naked)) void SDL_RenderPresent() { asm("jmp *%0" : : "m"(p_SDL_RenderPresent)); }
__attribute__((naked)) void SDL_RenderReadPixels() { asm("jmp *%0" : : "m"(p_SDL_RenderReadPixels)); }
__attribute__((naked)) void SDL_RenderSetClipRect() { asm("jmp *%0" : : "m"(p_SDL_RenderSetClipRect)); }
__attribute__((naked)) void SDL_RenderSetLogicalSize() { asm("jmp *%0" : : "m"(p_SDL_RenderSetLogicalSize)); }
__attribute__((naked)) void SDL_RenderSetScale() { asm("jmp *%0" : : "m"(p_SDL_RenderSetScale)); }
__attribute__((naked)) void SDL_RenderSetViewport() { asm("jmp *%0" : : "m"(p_SDL_RenderSetViewport)); }
__attribute__((naked)) void SDL_RenderTargetSupported() { asm("jmp *%0" : : "m"(p_SDL_RenderTargetSupported)); }
__attribute__((naked)) void SDL_ReportAssertion() { asm("jmp *%0" : : "m"(p_SDL_ReportAssertion)); }
__attribute__((naked)) void SDL_ResetAssertionReport() { asm("jmp *%0" : : "m"(p_SDL_ResetAssertionReport)); }
__attribute__((naked)) void SDL_RestoreWindow() { asm("jmp *%0" : : "m"(p_SDL_RestoreWindow)); }
__attribute__((naked)) void SDL_SaveAllDollarTemplates() { asm("jmp *%0" : : "m"(p_SDL_SaveAllDollarTemplates)); }
__attribute__((naked)) void SDL_SaveBMP_RW() { asm("jmp *%0" : : "m"(p_SDL_SaveBMP_RW)); }
__attribute__((naked)) void SDL_SaveDollarTemplate() { asm("jmp *%0" : : "m"(p_SDL_SaveDollarTemplate)); }
__attribute__((naked)) void SDL_SemPost() { asm("jmp *%0" : : "m"(p_SDL_SemPost)); }
__attribute__((naked)) void SDL_SemTryWait() { asm("jmp *%0" : : "m"(p_SDL_SemTryWait)); }
__attribute__((naked)) void SDL_SemValue() { asm("jmp *%0" : : "m"(p_SDL_SemValue)); }
__attribute__((naked)) void SDL_SemWait() { asm("jmp *%0" : : "m"(p_SDL_SemWait)); }
__attribute__((naked)) void SDL_SemWaitTimeout() { asm("jmp *%0" : : "m"(p_SDL_SemWaitTimeout)); }
__attribute__((naked)) void SDL_SetAssertionHandler() { asm("jmp *%0" : : "m"(p_SDL_SetAssertionHandler)); }
__attribute__((naked)) void SDL_SetClipRect() { asm("jmp *%0" : : "m"(p_SDL_SetClipRect)); }
__attribute__((naked)) void SDL_SetClipboardText() { asm("jmp *%0" : : "m"(p_SDL_SetClipboardText)); }
__attribute__((naked)) void SDL_SetColorKey() { asm("jmp *%0" : : "m"(p_SDL_SetColorKey)); }
__attribute__((naked)) void SDL_SetCursor() { asm("jmp *%0" : : "m"(p_SDL_SetCursor)); }
__attribute__((naked)) void SDL_SetError() { asm("jmp *%0" : : "m"(p_SDL_SetError)); }
__attribute__((naked)) void SDL_SetEventFilter() { asm("jmp *%0" : : "m"(p_SDL_SetEventFilter)); }
__attribute__((naked)) void SDL_SetHint() { asm("jmp *%0" : : "m"(p_SDL_SetHint)); }
__attribute__((naked)) void SDL_SetHintWithPriority() { asm("jmp *%0" : : "m"(p_SDL_SetHintWithPriority)); }
__attribute__((naked)) void SDL_SetMainReady() { asm("jmp *%0" : : "m"(p_SDL_SetMainReady)); }
__attribute__((naked)) void SDL_SetModState() { asm("jmp *%0" : : "m"(p_SDL_SetModState)); }
__attribute__((naked)) void SDL_SetPaletteColors() { asm("jmp *%0" : : "m"(p_SDL_SetPaletteColors)); }
__attribute__((naked)) void SDL_SetPixelFormatPalette() { asm("jmp *%0" : : "m"(p_SDL_SetPixelFormatPalette)); }
__attribute__((naked)) void SDL_SetRelativeMouseMode() { asm("jmp *%0" : : "m"(p_SDL_SetRelativeMouseMode)); }
__attribute__((naked)) void SDL_SetRenderDrawBlendMode() { asm("jmp *%0" : : "m"(p_SDL_SetRenderDrawBlendMode)); }
__attribute__((naked)) void SDL_SetRenderDrawColor() { asm("jmp *%0" : : "m"(p_SDL_SetRenderDrawColor)); }
__attribute__((naked)) void SDL_SetRenderTarget() { asm("jmp *%0" : : "m"(p_SDL_SetRenderTarget)); }
__attribute__((naked)) void SDL_SetSurfaceAlphaMod() { asm("jmp *%0" : : "m"(p_SDL_SetSurfaceAlphaMod)); }
__attribute__((naked)) void SDL_SetSurfaceBlendMode() { asm("jmp *%0" : : "m"(p_SDL_SetSurfaceBlendMode)); }
__attribute__((naked)) void SDL_SetSurfaceColorMod() { asm("jmp *%0" : : "m"(p_SDL_SetSurfaceColorMod)); }
__attribute__((naked)) void SDL_SetSurfacePalette() { asm("jmp *%0" : : "m"(p_SDL_SetSurfacePalette)); }
__attribute__((naked)) void SDL_SetSurfaceRLE() { asm("jmp *%0" : : "m"(p_SDL_SetSurfaceRLE)); }
__attribute__((naked)) void SDL_SetTextInputRect() { asm("jmp *%0" : : "m"(p_SDL_SetTextInputRect)); }
__attribute__((naked)) void SDL_SetTextureAlphaMod() { asm("jmp *%0" : : "m"(p_SDL_SetTextureAlphaMod)); }
__attribute__((naked)) void SDL_SetTextureBlendMode() { asm("jmp *%0" : : "m"(p_SDL_SetTextureBlendMode)); }
__attribute__((naked)) void SDL_SetTextureColorMod() { asm("jmp *%0" : : "m"(p_SDL_SetTextureColorMod)); }
__attribute__((naked)) void SDL_SetThreadPriority() { asm("jmp *%0" : : "m"(p_SDL_SetThreadPriority)); }
__attribute__((naked)) void SDL_SetWindowBordered() { asm("jmp *%0" : : "m"(p_SDL_SetWindowBordered)); }
__attribute__((naked)) void SDL_SetWindowBrightness() { asm("jmp *%0" : : "m"(p_SDL_SetWindowBrightness)); }
__attribute__((naked)) void SDL_SetWindowData() { asm("jmp *%0" : : "m"(p_SDL_SetWindowData)); }
__attribute__((naked)) void SDL_SetWindowDisplayMode() { asm("jmp *%0" : : "m"(p_SDL_SetWindowDisplayMode)); }
__attribute__((naked)) void SDL_SetWindowFullscreen() { asm("jmp *%0" : : "m"(p_SDL_SetWindowFullscreen)); }
__attribute__((naked)) void SDL_SetWindowGammaRamp() { asm("jmp *%0" : : "m"(p_SDL_SetWindowGammaRamp)); }
__attribute__((naked)) void SDL_SetWindowGrab() { asm("jmp *%0" : : "m"(p_SDL_SetWindowGrab)); }
__attribute__((naked)) void SDL_SetWindowIcon() { asm("jmp *%0" : : "m"(p_SDL_SetWindowIcon)); }
__attribute__((naked)) void SDL_SetWindowMaximumSize() { asm("jmp *%0" : : "m"(p_SDL_SetWindowMaximumSize)); }
__attribute__((naked)) void SDL_SetWindowMinimumSize() { asm("jmp *%0" : : "m"(p_SDL_SetWindowMinimumSize)); }
__attribute__((naked)) void SDL_SetWindowPosition() { asm("jmp *%0" : : "m"(p_SDL_SetWindowPosition)); }
__attribute__((naked)) void SDL_SetWindowShape() { asm("jmp *%0" : : "m"(p_SDL_SetWindowShape)); }
__attribute__((naked)) void SDL_SetWindowSize() { asm("jmp *%0" : : "m"(p_SDL_SetWindowSize)); }
__attribute__((naked)) void SDL_SetWindowTitle() { asm("jmp *%0" : : "m"(p_SDL_SetWindowTitle)); }
__attribute__((naked)) void SDL_ShowCursor() { asm("jmp *%0" : : "m"(p_SDL_ShowCursor)); }
__attribute__((naked)) void SDL_ShowMessageBox() { asm("jmp *%0" : : "m"(p_SDL_ShowMessageBox)); }
__attribute__((naked)) void SDL_ShowSimpleMessageBox() { asm("jmp *%0" : : "m"(p_SDL_ShowSimpleMessageBox)); }
__attribute__((naked)) void SDL_ShowWindow() { asm("jmp *%0" : : "m"(p_SDL_ShowWindow)); }
__attribute__((naked)) void SDL_SoftStretch() { asm("jmp *%0" : : "m"(p_SDL_SoftStretch)); }
__attribute__((naked)) void SDL_StartTextInput() { asm("jmp *%0" : : "m"(p_SDL_StartTextInput)); }
__attribute__((naked)) void SDL_StopTextInput() { asm("jmp *%0" : : "m"(p_SDL_StopTextInput)); }
__attribute__((naked)) void SDL_TLSCreate() { asm("jmp *%0" : : "m"(p_SDL_TLSCreate)); }
__attribute__((naked)) void SDL_TLSGet() { asm("jmp *%0" : : "m"(p_SDL_TLSGet)); }
__attribute__((naked)) void SDL_TLSSet() { asm("jmp *%0" : : "m"(p_SDL_TLSSet)); }
__attribute__((naked)) void SDL_ThreadID() { asm("jmp *%0" : : "m"(p_SDL_ThreadID)); }
__attribute__((naked)) void SDL_TryLockMutex() { asm("jmp *%0" : : "m"(p_SDL_TryLockMutex)); }
__attribute__((naked)) void SDL_UnionRect() { asm("jmp *%0" : : "m"(p_SDL_UnionRect)); }
__attribute__((naked)) void SDL_UnloadObject() { asm("jmp *%0" : : "m"(p_SDL_UnloadObject)); }
__attribute__((naked)) void SDL_UnlockAudio() { asm("jmp *%0" : : "m"(p_SDL_UnlockAudio)); }
__attribute__((naked)) void SDL_UnlockAudioDevice() { asm("jmp *%0" : : "m"(p_SDL_UnlockAudioDevice)); }
__attribute__((naked)) void SDL_UnlockMutex() { asm("jmp *%0" : : "m"(p_SDL_UnlockMutex)); }
__attribute__((naked)) void SDL_UnlockSurface() { asm("jmp *%0" : : "m"(p_SDL_UnlockSurface)); }
__attribute__((naked)) void SDL_UnlockTexture() { asm("jmp *%0" : : "m"(p_SDL_UnlockTexture)); }
__attribute__((naked)) void SDL_UnregisterApp() { asm("jmp *%0" : : "m"(p_SDL_UnregisterApp)); }
__attribute__((naked)) void SDL_UpdateTexture() { asm("jmp *%0" : : "m"(p_SDL_UpdateTexture)); }
__attribute__((naked)) void SDL_UpdateWindowSurface() { asm("jmp *%0" : : "m"(p_SDL_UpdateWindowSurface)); }
__attribute__((naked)) void SDL_UpdateWindowSurfaceRects() { asm("jmp *%0" : : "m"(p_SDL_UpdateWindowSurfaceRects)); }
__attribute__((naked)) void SDL_UpdateYUVTexture() { asm("jmp *%0" : : "m"(p_SDL_UpdateYUVTexture)); }
__attribute__((naked)) void SDL_UpperBlit() { asm("jmp *%0" : : "m"(p_SDL_UpperBlit)); }
__attribute__((naked)) void SDL_UpperBlitScaled() { asm("jmp *%0" : : "m"(p_SDL_UpperBlitScaled)); }
__attribute__((naked)) void SDL_VideoInit() { asm("jmp *%0" : : "m"(p_SDL_VideoInit)); }
__attribute__((naked)) void SDL_VideoQuit() { asm("jmp *%0" : : "m"(p_SDL_VideoQuit)); }
__attribute__((naked)) void SDL_WaitEvent() { asm("jmp *%0" : : "m"(p_SDL_WaitEvent)); }
__attribute__((naked)) void SDL_WaitEventTimeout() { asm("jmp *%0" : : "m"(p_SDL_WaitEventTimeout)); }
__attribute__((naked)) void SDL_WaitThread() { asm("jmp *%0" : : "m"(p_SDL_WaitThread)); }
__attribute__((naked)) void SDL_WarpMouseInWindow() { asm("jmp *%0" : : "m"(p_SDL_WarpMouseInWindow)); }
__attribute__((naked)) void SDL_WasInit() { asm("jmp *%0" : : "m"(p_SDL_WasInit)); }
__attribute__((naked)) void SDL_WriteBE16() { asm("jmp *%0" : : "m"(p_SDL_WriteBE16)); }
__attribute__((naked)) void SDL_WriteBE32() { asm("jmp *%0" : : "m"(p_SDL_WriteBE32)); }
__attribute__((naked)) void SDL_WriteBE64() { asm("jmp *%0" : : "m"(p_SDL_WriteBE64)); }
__attribute__((naked)) void SDL_WriteLE16() { asm("jmp *%0" : : "m"(p_SDL_WriteLE16)); }
__attribute__((naked)) void SDL_WriteLE32() { asm("jmp *%0" : : "m"(p_SDL_WriteLE32)); }
__attribute__((naked)) void SDL_WriteLE64() { asm("jmp *%0" : : "m"(p_SDL_WriteLE64)); }
__attribute__((naked)) void SDL_WriteU8() { asm("jmp *%0" : : "m"(p_SDL_WriteU8)); }
__attribute__((naked)) void SDL_abs() { asm("jmp *%0" : : "m"(p_SDL_abs)); }
__attribute__((naked)) void SDL_acos() { asm("jmp *%0" : : "m"(p_SDL_acos)); }
__attribute__((naked)) void SDL_asin() { asm("jmp *%0" : : "m"(p_SDL_asin)); }
__attribute__((naked)) void SDL_atan() { asm("jmp *%0" : : "m"(p_SDL_atan)); }
__attribute__((naked)) void SDL_atan2() { asm("jmp *%0" : : "m"(p_SDL_atan2)); }
__attribute__((naked)) void SDL_atof() { asm("jmp *%0" : : "m"(p_SDL_atof)); }
__attribute__((naked)) void SDL_atoi() { asm("jmp *%0" : : "m"(p_SDL_atoi)); }
__attribute__((naked)) void SDL_calloc() { asm("jmp *%0" : : "m"(p_SDL_calloc)); }
__attribute__((naked)) void SDL_ceil() { asm("jmp *%0" : : "m"(p_SDL_ceil)); }
__attribute__((naked)) void SDL_copysign() { asm("jmp *%0" : : "m"(p_SDL_copysign)); }
__attribute__((naked)) void SDL_cos() { asm("jmp *%0" : : "m"(p_SDL_cos)); }
__attribute__((naked)) void SDL_cosf() { asm("jmp *%0" : : "m"(p_SDL_cosf)); }
__attribute__((naked)) void SDL_fabs() { asm("jmp *%0" : : "m"(p_SDL_fabs)); }
__attribute__((naked)) void SDL_floor() { asm("jmp *%0" : : "m"(p_SDL_floor)); }
__attribute__((naked)) void SDL_free() { asm("jmp *%0" : : "m"(p_SDL_free)); }
__attribute__((naked)) void SDL_getenv() { asm("jmp *%0" : : "m"(p_SDL_getenv)); }
__attribute__((naked)) void SDL_iconv() { asm("jmp *%0" : : "m"(p_SDL_iconv)); }
__attribute__((naked)) void SDL_iconv_close() { asm("jmp *%0" : : "m"(p_SDL_iconv_close)); }
__attribute__((naked)) void SDL_iconv_open() { asm("jmp *%0" : : "m"(p_SDL_iconv_open)); }
__attribute__((naked)) void SDL_iconv_string() { asm("jmp *%0" : : "m"(p_SDL_iconv_string)); }
__attribute__((naked)) void SDL_isdigit() { asm("jmp *%0" : : "m"(p_SDL_isdigit)); }
__attribute__((naked)) void SDL_isspace() { asm("jmp *%0" : : "m"(p_SDL_isspace)); }
__attribute__((naked)) void SDL_itoa() { asm("jmp *%0" : : "m"(p_SDL_itoa)); }
__attribute__((naked)) void SDL_lltoa() { asm("jmp *%0" : : "m"(p_SDL_lltoa)); }
__attribute__((naked)) void SDL_log() { asm("jmp *%0" : : "m"(p_SDL_log)); }
__attribute__((naked)) void SDL_ltoa() { asm("jmp *%0" : : "m"(p_SDL_ltoa)); }
__attribute__((naked)) void SDL_malloc() { asm("jmp *%0" : : "m"(p_SDL_malloc)); }
__attribute__((naked)) void SDL_memcmp() { asm("jmp *%0" : : "m"(p_SDL_memcmp)); }
__attribute__((naked)) void SDL_memcpy() { asm("jmp *%0" : : "m"(p_SDL_memcpy)); }
__attribute__((naked)) void SDL_memmove() { asm("jmp *%0" : : "m"(p_SDL_memmove)); }
__attribute__((naked)) void SDL_memset() { asm("jmp *%0" : : "m"(p_SDL_memset)); }
__attribute__((naked)) void SDL_pow() { asm("jmp *%0" : : "m"(p_SDL_pow)); }
__attribute__((naked)) void SDL_qsort() { asm("jmp *%0" : : "m"(p_SDL_qsort)); }
__attribute__((naked)) void SDL_realloc() { asm("jmp *%0" : : "m"(p_SDL_realloc)); }
__attribute__((naked)) void SDL_scalbn() { asm("jmp *%0" : : "m"(p_SDL_scalbn)); }
__attribute__((naked)) void SDL_setenv() { asm("jmp *%0" : : "m"(p_SDL_setenv)); }
__attribute__((naked)) void SDL_sin() { asm("jmp *%0" : : "m"(p_SDL_sin)); }
__attribute__((naked)) void SDL_sinf() { asm("jmp *%0" : : "m"(p_SDL_sinf)); }
__attribute__((naked)) void SDL_snprintf() { asm("jmp *%0" : : "m"(p_SDL_snprintf)); }
__attribute__((naked)) void SDL_sqrt() { asm("jmp *%0" : : "m"(p_SDL_sqrt)); }
__attribute__((naked)) void SDL_sscanf() { asm("jmp *%0" : : "m"(p_SDL_sscanf)); }
__attribute__((naked)) void SDL_strcasecmp() { asm("jmp *%0" : : "m"(p_SDL_strcasecmp)); }
__attribute__((naked)) void SDL_strchr() { asm("jmp *%0" : : "m"(p_SDL_strchr)); }
__attribute__((naked)) void SDL_strcmp() { asm("jmp *%0" : : "m"(p_SDL_strcmp)); }
__attribute__((naked)) void SDL_strdup() { asm("jmp *%0" : : "m"(p_SDL_strdup)); }
__attribute__((naked)) void SDL_strlcat() { asm("jmp *%0" : : "m"(p_SDL_strlcat)); }
__attribute__((naked)) void SDL_strlcpy() { asm("jmp *%0" : : "m"(p_SDL_strlcpy)); }
__attribute__((naked)) void SDL_strlen() { asm("jmp *%0" : : "m"(p_SDL_strlen)); }
__attribute__((naked)) void SDL_strlwr() { asm("jmp *%0" : : "m"(p_SDL_strlwr)); }
__attribute__((naked)) void SDL_strncasecmp() { asm("jmp *%0" : : "m"(p_SDL_strncasecmp)); }
__attribute__((naked)) void SDL_strncmp() { asm("jmp *%0" : : "m"(p_SDL_strncmp)); }
__attribute__((naked)) void SDL_strrchr() { asm("jmp *%0" : : "m"(p_SDL_strrchr)); }
__attribute__((naked)) void SDL_strrev() { asm("jmp *%0" : : "m"(p_SDL_strrev)); }
__attribute__((naked)) void SDL_strstr() { asm("jmp *%0" : : "m"(p_SDL_strstr)); }
__attribute__((naked)) void SDL_strtod() { asm("jmp *%0" : : "m"(p_SDL_strtod)); }
__attribute__((naked)) void SDL_strtol() { asm("jmp *%0" : : "m"(p_SDL_strtol)); }
__attribute__((naked)) void SDL_strtoll() { asm("jmp *%0" : : "m"(p_SDL_strtoll)); }
__attribute__((naked)) void SDL_strtoul() { asm("jmp *%0" : : "m"(p_SDL_strtoul)); }
__attribute__((naked)) void SDL_strtoull() { asm("jmp *%0" : : "m"(p_SDL_strtoull)); }
__attribute__((naked)) void SDL_strupr() { asm("jmp *%0" : : "m"(p_SDL_strupr)); }
__attribute__((naked)) void SDL_tolower() { asm("jmp *%0" : : "m"(p_SDL_tolower)); }
__attribute__((naked)) void SDL_toupper() { asm("jmp *%0" : : "m"(p_SDL_toupper)); }
__attribute__((naked)) void SDL_uitoa() { asm("jmp *%0" : : "m"(p_SDL_uitoa)); }
__attribute__((naked)) void SDL_ulltoa() { asm("jmp *%0" : : "m"(p_SDL_ulltoa)); }
__attribute__((naked)) void SDL_ultoa() { asm("jmp *%0" : : "m"(p_SDL_ultoa)); }
__attribute__((naked)) void SDL_utf8strlcpy() { asm("jmp *%0" : : "m"(p_SDL_utf8strlcpy)); }
__attribute__((naked)) void SDL_vsnprintf() { asm("jmp *%0" : : "m"(p_SDL_vsnprintf)); }
__attribute__((naked)) void SDL_vsscanf() { asm("jmp *%0" : : "m"(p_SDL_vsscanf)); }
__attribute__((naked)) void SDL_wcslcat() { asm("jmp *%0" : : "m"(p_SDL_wcslcat)); }
__attribute__((naked)) void SDL_wcslcpy() { asm("jmp *%0" : : "m"(p_SDL_wcslcpy)); }
__attribute__((naked)) void SDL_wcslen() { asm("jmp *%0" : : "m"(p_SDL_wcslen)); }
