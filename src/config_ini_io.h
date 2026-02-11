#pragma once
#include <SDL3/SDL.h>

#define CONFIG_FILE_NAME "config.ini"

typedef struct
{
    struct
    {
        int window_width, window_height, window_mode;
    } DisplaySettings;
    struct
    {
        int msaa_level;
        int vsync;        
    } GraphicsSettings;
} ConfigData;

#pragma warning(push, 0)
#include "generated/config_functions.h"
#pragma warning(pop)

void SaveConfig(ConfigData *config)
{
    SDL_IOStream *file = SDL_IOFromFile(CONFIG_FILE_NAME, "wb");
    if (!file)
        return;

    char buffer[256];
    Generated_SaveConfigToString(config, buffer, 256);

    SDL_WriteIO(file, buffer, SDL_strlen(buffer));
    SDL_CloseIO(file);
}

ConfigData LoadConfig()
{
    ConfigData config = {};
    SDL_IOStream *file = SDL_IOFromFile(CONFIG_FILE_NAME, "rb");
    if (!file)
    {
        // default settings here
        config.DisplaySettings.window_width = 1920;
        config.DisplaySettings.window_height = 1080;
        config.DisplaySettings.window_mode = (int)1;
        config.GraphicsSettings.msaa_level = 1;
        config.GraphicsSettings.vsync = 0;
        SaveConfig(&config);
        return config;
    }

    Sint64 size = SDL_GetIOSize(file);
    char *data = (char *)SDL_malloc((size_t)(size + 1));
    SDL_ReadIO(file, data, (size_t)size);
    data[size] = 0;
    SDL_CloseIO(file);

    Generated_LoadConfigFromString(&config, data);

    SDL_free(data);

    return config;
}