#include "theme.h"

#include "raygui.h"

UITheme g_theme = {0};

/**
 * Set tema default (Dark / Monokai Inspired)
 */
void Theme_default(void) {
    g_theme.bg_main = (Color){18, 18, 20, 255};
    g_theme.bg_editor = (Color){25, 25, 28, 255};
    g_theme.bg_sidebar = (Color){32, 33, 40, 255};  // Dipakai untuk Tab Bar & Status Bar
    g_theme.bg_card = (Color){38, 40, 50, 245};     // Dipakai untuk Modal / Pop-up
    g_theme.border = (Color){60, 65, 80, 255};
    g_theme.active_tab = (Color){50, 52, 65, 255};
    g_theme.active_line = (Color){255, 255, 255, 12};
    g_theme.line_num = (Color){224, 175, 104, 255};
    g_theme.backdrop = (Color){0, 0, 0, 140};  // Hitam transparan saat dialog/overlay

    g_theme.selection = (Color){50, 75, 120, 255};
    g_theme.cursor = (Color){240, 190, 50, 255};
    g_theme.accent = (Color){80, 140, 240, 255};

    g_theme.text_normal = (Color){200, 205, 215, 255};
    g_theme.text_muted = (Color){90, 98, 120, 255};  // Komentar & teks muted
    g_theme.text_highlight = (Color){255, 255, 255, 255};

    g_theme.success = (Color){80, 200, 120, 255};
    g_theme.warning = (Color){240, 180, 70, 255};
    g_theme.error = (Color){240, 80, 80, 255};
    g_theme.info = (Color){33, 150, 243, 230};

    // Syntax Highlighting
    g_theme.keyword = (Color){249, 38, 114, 255};   // Pink / Red
    g_theme.type = (Color){102, 217, 239, 255};     // Cyan
    g_theme.comment = (Color){90, 98, 120, 255};    // Disamakan dengan text_muted
    g_theme.string = (Color){230, 219, 116, 255};   // Yellow
    g_theme.function = (Color){166, 226, 46, 255};  // Green
    g_theme.number = (Color){174, 129, 255, 255};   // Purple
    g_theme.method = (Color){166, 226, 46, 255};    // Green
    g_theme.operator = (Color){249, 38, 114, 255};  // Pink / Red
    g_theme.constant = (Color){253, 151, 31, 255};  // Orange
                                                    //
    // Bracket Match
    g_theme.bracket_match = (Color){249, 38, 114, 255};     // Pink / Magenta
    g_theme.bracket_match_bg = (Color){255, 255, 255, 35};  // White translucent
}

/**
 * Set tema Tokyo Night (Official Palette)
 */
static void Theme_load_tokyo_night(void) {
    g_theme.bg_main = (Color){26, 27, 38, 255};     // Night background
    g_theme.bg_editor = (Color){30, 32, 48, 255};   // Editor area
    g_theme.bg_sidebar = (Color){36, 40, 59, 255};  // Status bar & Tab bar
    g_theme.bg_card = (Color){41, 44, 66, 250};     // Pop-up / Card background
    g_theme.border = (Color){66, 72, 104, 255};
    g_theme.active_tab = (Color){54, 58, 88, 255};
    g_theme.active_line = (Color){255, 255, 255, 14};
    g_theme.line_num = (Color){125, 135, 178, 255};
    g_theme.backdrop = (Color){15, 15, 24, 160};  // Transparan gelap dengan sedikit hint navy

    g_theme.selection = (Color){65, 72, 104, 255};
    g_theme.cursor = (Color){224, 175, 104, 255};  // Yellow/Orange Cursor
    g_theme.accent = (Color){122, 162, 247, 255};  // Tokyo Blue

    g_theme.text_normal = (Color){192, 202, 245, 255};
    g_theme.text_muted = (Color){86, 95, 137, 255};  // Comment & Muted text
    g_theme.text_highlight = (Color){255, 255, 255, 255};

    g_theme.success = (Color){158, 206, 106, 255};
    g_theme.warning = (Color){224, 175, 104, 255};
    g_theme.error = (Color){247, 118, 142, 255};
    g_theme.info = (Color){122, 162, 247, 255};

    // Syntax Highlighting Tokyo Night
    g_theme.keyword = (Color){187, 154, 247, 255};   // Magenta / Purple
    g_theme.type = (Color){42, 195, 222, 255};       // Cyan
    g_theme.comment = (Color){86, 95, 137, 255};     // Disamakan dengan text_muted!
    g_theme.string = (Color){158, 206, 106, 255};    // Green
    g_theme.function = (Color){122, 162, 247, 255};  // Blue
    g_theme.number = (Color){255, 158, 100, 255};    // Orange
    g_theme.method = (Color){122, 162, 247, 255};    // Blue
    g_theme.operator = (Color){137, 221, 254, 255};  // Cyan / Light Blue
    g_theme.constant = (Color){255, 158, 100, 255};  // Orange

    // Bracket Match
    g_theme.bracket_match = (Color){187, 154, 247, 255};   // Tokyo Purple
    g_theme.bracket_match_bg = (Color){61, 89, 161, 120};  // Blue Tint Translucence
}

/**
 * Init Theme [PUBLIC API]
 */
void Theme_init(ThemePreset preset) {
    switch (preset) {
        case DEFAULT_THEME:
            Theme_default();
            break;
        case TOKYO_NIGHT:
            Theme_load_tokyo_night();
            break;
    }
    Theme_apply_raygui();
}

/**
 * Apply Theme ke RayGUI
 */
void Theme_apply_raygui(void) {
    // Window / Main Background
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(g_theme.bg_main));

    // Tab Bar & Status Bar Area
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, ColorToInt(g_theme.bg_sidebar));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, ColorToInt(g_theme.active_tab));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, ColorToInt(g_theme.accent));

    // Pop-up / Modal Card Area
    GuiSetStyle(LABEL, BACKGROUND_COLOR, ColorToInt(g_theme.bg_card));

    // Borders
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(g_theme.border));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(g_theme.accent));

    // Text Colors
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(g_theme.text_normal));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(g_theme.text_highlight));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(g_theme.text_highlight));
}
