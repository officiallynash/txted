/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdbool.h>

#include "raylib.h"

typedef enum { NOTIF_INFO, NOTIF_SUCCESS, NOTIF_WARNING, NOTIF_ERROR } NotifType;
typedef struct NotificationManager NotificationManager;

extern NotificationManager notif;  // Global instance

// Global instance atau helper functions
void Notif_init(void);
void Notif_show(const char *msg, NotifType type, float duration_sec);
void Notif_update(float delta_time);
void Notif_draw(Font font);

#endif
