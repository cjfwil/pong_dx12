#pragma once
#include <SDL3/SDL.h>

#define CONFIG_FILE_NAME "config.ini"

typedef struct {
    int width, height, mode;
} WindowConfig;

void SaveConfig(WindowConfig* config) {
    SDL_IOStream* file = SDL_IOFromFile(CONFIG_FILE_NAME, "wb");
    if (!file) return;
    
    char buffer[256];
    SDL_snprintf(buffer, sizeof(buffer), 
                 "width=%d\nheight=%d\nmode=%d\n", 
                 config->width, config->height, config->mode);
    
    SDL_WriteIO(file, buffer, SDL_strlen(buffer));
    SDL_CloseIO(file);
}

WindowConfig LoadConfig() {
    WindowConfig config = {640, 480, 0}; // default settings here
    
    SDL_IOStream* file = SDL_IOFromFile(CONFIG_FILE_NAME, "rb");
    if (!file) {
        SaveConfig(&config);
        return config;
    }
        
    Sint64 size = SDL_GetIOSize(file);
    char* data = (char*)SDL_malloc(size + 1);
    SDL_ReadIO(file, data, size);
    data[size] = 0;
    SDL_CloseIO(file);
        
    char* line = data;
    while (*line) {
        if (SDL_strncmp(line, "width=", 6) == 0) {
            config.width = SDL_atoi(line + 6);
        } else if (SDL_strncmp(line, "height=", 7) == 0) {
            config.height = SDL_atoi(line + 7);
        } else if (SDL_strncmp(line, "mode=", 5) == 0) {
            config.mode = SDL_atoi(line + 5);
        }
                
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }
    
    SDL_free(data);
        
    if (config.width < 640) config.width = 640;
    if (config.height < 480) config.height = 480;
    if (config.mode > 2) config.mode = 1;
    
    return config;
}