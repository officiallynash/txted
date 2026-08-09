#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    NOTIF_INFO,
    NOTIF_SUCCESS,
    NOTIF_WARNING,
    NOTIF_ERROR
} NotifType;

typedef struct {
    char message[256];
    NotifType type;
    float timer;       // Durasi tersisa dalam detik
    float max_duration;// Total durasi awal (buat animasi fade out)
    bool active;
} NotificationManager;

extern NotificationManager notif; // Global instance

// Global instance atau helper functions
void Notif_init(void);
void Notif_show(const char *msg, NotifType type, float duration_sec);
void Notif_update(float delta_time);
void Notif_draw(Font font);

#endif
