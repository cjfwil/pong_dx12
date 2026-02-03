#pragma once

inline void log_error(const char *msg = "Unknown error")
{
    SDL_Log("[ERROR]: %s", msg);
}