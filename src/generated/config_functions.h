//------------------------------------------------------------------------
// GENERATED CONFIG FUNCTIONS – DO NOT EDIT
//   This file was automatically generated.
//   by meta_config.py
//   Generated: 2026-02-13 08:08:46
//------------------------------------------------------------------------

#pragma once
#include <SDL3/SDL.h>

/* Inline function to generate the config string with sections */
static inline void Generated_SaveConfigToString(ConfigData* config, char* buffer, size_t buffer_size) {
    SDL_snprintf(buffer, buffer_size, 
                 "[DisplaySettings]\nwindow_width=%d\nwindow_height=%d\nwindow_mode=%d\n\n[GraphicsSettings]\nmsaa_level=%d\nvsync=%d\n", 
                 config->DisplaySettings.window_width,
                 config->DisplaySettings.window_height,
                 config->DisplaySettings.window_mode,
                 config->GraphicsSettings.msaa_level,
                 config->GraphicsSettings.vsync);
}

/* Inline function to parse config from string data with sections */
static inline void Generated_LoadConfigFromString(ConfigData* config, char* data) {
    char* line = data;
    char current_section[64] = {0};

    while (*line) {
        // Skip whitespace
        while (*line == ' ' || *line == '\t') line++;

        // Check for section header
        if (*line == '[') {
            char* section_end = SDL_strchr(line, ']');
            if (section_end) {
                size_t len = section_end - line - 1;
                if (len < sizeof(current_section) - 1) {
                    SDL_strlcpy(current_section, line + 1, len + 1);
                }
                line = section_end + 1;
            }
        } else {
            // Parse key=value pairs
            if (SDL_strcmp(current_section, "DisplaySettings") == 0) {
                if (SDL_strncmp(line, "window_width=", 13) == 0) {
                    config->DisplaySettings.window_width = SDL_atoi(line + 13);
                } else if (SDL_strncmp(line, "window_height=", 14) == 0) {
                    config->DisplaySettings.window_height = SDL_atoi(line + 14);
                } else if (SDL_strncmp(line, "window_mode=", 12) == 0) {
                    config->DisplaySettings.window_mode = SDL_atoi(line + 12);
                }
            }
            if (SDL_strcmp(current_section, "GraphicsSettings") == 0) {
                if (SDL_strncmp(line, "msaa_level=", 11) == 0) {
                    config->GraphicsSettings.msaa_level = SDL_atoi(line + 11);
                } else if (SDL_strncmp(line, "vsync=", 6) == 0) {
                    config->GraphicsSettings.vsync = SDL_atoi(line + 6);
                }
            }

            // Skip to next line
            while (*line && *line != '\n') line++;
        }

        if (*line == '\n') line++;
    }
}
