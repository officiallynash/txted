#include "notification.h"

#include <string.h>

#include "theme.h"
#include "ui.h"

NotificationManager notif = {0};  // Global instance

/**
 * Inisialisasi Notification
 */
void Notif_init(void) {
    notif.message[0] = '\0';
    notif.timer = 0.0f;
    notif.max_duration = 0.0f;
    notif.active = false;
}

/**
 * Tampilkan Notification
 */
void Notif_show(const char *msg, NotifType type, float duration_sec) {
    if (!msg) return;

    strncpy(notif.message, msg, sizeof(notif.message) - 1);
    notif.message[sizeof(notif.message) - 1] = '\0';

    notif.type = type;
    notif.timer = duration_sec;
    notif.max_duration = duration_sec;
    notif.active = true;
}

/**
 * Update Notification
 */
void Notif_update(float delta_time) {
    notif.timer -= delta_time;
    if (notif.timer <= 0.0f) {
        notif.timer = 0.0f;
        notif.active = false;
    }
}

/**
 * Render Notification
 */
void Notif_draw(Font font) {
    int win_w = GetRenderWidth();
    int win_h = GetRenderHeight();

    // Menentukan Warna berdasarkan Tipe
    Color bg_color;
    Color border_color = g_theme.border;
    Color text_color = WHITE;

    switch (notif.type) {
        case NOTIF_SUCCESS:
            bg_color = g_theme.success;  // Hijau
            break;
        case NOTIF_WARNING:
            bg_color = g_theme.warning;  // Kuning
            text_color = BLACK;
            break;
        case NOTIF_ERROR:
            bg_color = g_theme.error;  // Merah
            break;
        case NOTIF_INFO:
        default:
            bg_color = g_theme.info;  // Biru
            break;
    }

    // Menghitung Efek Fade (Alpha Transparent)
    float alpha = 1.0f;
    if (notif.timer < 0.5f) {  // 0.5 detik terakhir akan fade out
        alpha = notif.timer / 0.5f;
    }

    bg_color.a = (unsigned char)(bg_color.a * alpha);
    border_color.a = (unsigned char)(border_color.a * alpha);
    text_color.a = (unsigned char)(255 * alpha);

    // Menghitung Ukuran Teks
    Vector2 text_size = MeasureTextEx(font, notif.message, FONT_SIZE, 1.0f);

    float padding_x = 20.0f;
    float padding_y = 12.0f;
    float box_w = text_size.x + (padding_x * 2);
    float box_h = text_size.y + (padding_y * 2);

    // Posisi di Pojok Kanan Bawah (di atas status bar)
    float pos_x = win_w - box_w - 20.0f;
    float pos_y = win_h - box_h - 40.0f;

    Rectangle rect = {pos_x, pos_y, box_w, box_h};

    // Render Box Notifikasi
    DrawRectangleRounded(rect, 0.2f, 4, bg_color);
    DrawRectangleRoundedLines(rect, 0.2f, 4, border_color);

    if (notif.max_duration > 0.0f) {
        float progress = notif.timer / notif.max_duration;
        float bar_w = (box_w - 8.0f) * progress;
        Rectangle bar_rect = {pos_x + 4.0f, pos_y + box_h - 4.0f, bar_w, 2.0f};
        Color bar_color = border_color;
        bar_color.a = (unsigned char)(200 * alpha);
        DrawRectangleRec(bar_rect, bar_color);
    }

    // Render Teks
    Vector2 text_pos = {pos_x + padding_x, pos_y + padding_y};
    DrawTextEx(font, notif.message, text_pos, FONT_SIZE, 1.0f, text_color);
}
