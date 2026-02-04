#pragma once

inline void log_error(const char *msg = "Unknown error")
{
    SDL_Log("[ERROR]: %s", msg);
}

inline void log_sdl_error(const char *msg = "Unknown SDL error")
{
    SDL_Log("[SDL ERROR]: %s: %s", msg, SDL_GetError());
}

inline const char *HrToString(HRESULT hr)
{
    thread_local static char buffer[512];

    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;

    DWORD len = FormatMessageA(
        flags,
        nullptr,
        (DWORD)hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        sizeof(buffer),
        nullptr);

    if (len == 0)
    {
        // Fallback if Windows has no message for this HRESULT
        wsprintfA(buffer, "Unknown HRESULT 0x%08X", (unsigned int)hr);
    }
    return buffer;
}

inline void log_hr_error(const char *msg = "Unknown HRESULT error", HRESULT hr = S_OK)
{
    SDL_Log("[HRESULT ERROR]: %s: %s", msg, HrToString(hr));
}

inline bool HRAssert(HRESULT hr, const char *msg = "HRESULT failed")
{
    if (SUCCEEDED(hr))
        return true;

    log_hr_error(msg, hr);

#if defined(_DEBUG) || defined(DEBUG)
    DebugBreak(); // break into debugger in debug builds
#endif

    return false;
}