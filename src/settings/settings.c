#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_txted.h"
#include "theme.h"

Settings default_settings = {
    .font_size = 18, .theme = DEFAULT_THEME, .font = "JetBrainsMono-Regular.ttf"};

static ThemePreset Load_theme(const char *theme_name) {
    if (strcmp(theme_name, "tokyonight") == 0) {
        return TOKYO_NIGHT;
    } else {
        return DEFAULT_THEME;
    }
}

void Settings_load(void) {
    char setting_path[128];
    snprintf(setting_path, sizeof(setting_path), "%s/settings/settings.ini",
             GetApplicationDirectory());

    FILE *fp = fopen(setting_path, "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], val[128];

        if (sscanf(line, " %63[^ =] = %127s", key, val) == 2) {
            val[strcspn(val, "\r\n")] = 0;

            // Font Size
            if (strcmp(key, "font_size") == 0) {
                default_settings.font_size = atoi(val);
            }
            // Theme
            else if (strcmp(key, "theme") == 0) {
                default_settings.theme = Load_theme(val);
            }
            // Font
            else if (strcmp(key, "font") == 0) {
                strncpy(default_settings.font, val, sizeof(default_settings.font));
            }
        }
        printf("%s %s", key, val);
    }
    fclose(fp);
}
