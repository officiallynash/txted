#ifndef SETTINGS_H
#define SETTINGS_H

#include "theme.h"

typedef struct {
    int font_size;
    ThemePreset theme;
    char font[128];
} Settings;

extern Settings default_settings;
void Settings_load(void);

#endif
