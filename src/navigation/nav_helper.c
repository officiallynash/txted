#include <raylib.h>
#include <string.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "raygui.h"
#include "rope.h"
#include "ui.h"

extern int visible_lines(void);  // Jumlah baris yang terlihat (render.c)
extern void expand_tabs(const char *src, char *dst, size_t dst_size,
                        int tab_size);  // expand_tabs (render.c)

/**
 * Mengatur kursor berdasarkan posisi mouse (Fixed Tab & Layout)
 */
void set_cursor_from_mouse(BufManager *bufmgr, Vector2 mouse, int scroll_y, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf || !buf->lines.offset) return;

    EditorLayout Layout = get_editor_layout(bufmgr);
    int editor_top = Layout.editor_y;  // Pake layout aktual!
    int editor_h = Layout.editor_h;

    if (mouse.x >= Layout.text_screen_x && mouse.y >= editor_top + PAD_Y &&
        mouse.y < editor_top + editor_h - PAD_Y) {
        // Hitung Baris Target (Y)
        int rel_y = (int)((mouse.y - (editor_top + PAD_Y)) / LINE_H);
        size_t target_y = (size_t)(scroll_y + rel_y);

        if (target_y >= buf->lines.line_count) {
            target_y = buf->lines.line_count > 0 ? buf->lines.line_count - 1 : 0;
        }

        buf->cursor.y = target_y;

        // Hitung Kolom Target (X)
        char *line_text = Buffer_get_line_text(buf, target_y);
        if (line_text) {
            size_t len = strlen(line_text);
            if (len > 0 && line_text[len - 1] == '\n') line_text[len - 1] = '\0';

            float space_w = MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;
            float click_x_rel = mouse.x - Layout.text_screen_x;

            float current_visual_x = 0.0f;
            size_t char_idx = 0;
            size_t orig_len = strlen(line_text);
            int col_visual = 0;

            while (char_idx < orig_len) {
                float advance = space_w;
                if (line_text[char_idx] == '\t') {
                    int spaces = 4 - (col_visual % 4);
                    advance = space_w * spaces;
                    col_visual += spaces;
                } else {
                    char ch[2] = {line_text[char_idx], '\0'};
                    advance = MeasureTextEx(font, ch, FONT_SIZE, 1.0f).x;
                    col_visual++;
                }

                // Jika klik mouse lebih dekat ke pertengahan karakter ini, berhenti
                if (click_x_rel < current_visual_x + (advance / 2.0f)) {
                    break;
                }

                current_visual_x += advance;
                char_idx++;
            }

            buf->cursor.x = char_idx;
            buf->cursor.cursor_pos = buf->lines.offset[target_y] + buf->cursor.x;

            free(line_text);
        } else {
            buf->cursor.x = 0;
            buf->cursor.cursor_pos = buf->lines.offset[target_y];
        }
    }
}

/**
 * Fungsi untuk sync Cursor
 */
void sync_cursor_line_from_pos(Buffer *buf) {
    if (!buf || buf->lines.line_count == 0) return;

    // Pastikan cursor_pos berada dalam range buffer yang valid
    if (buf->cursor.cursor_pos > buf->str->len) {
        buf->cursor.cursor_pos = buf->str->len;
    }

    size_t line = 0;
    for (size_t i = 0; i < buf->lines.line_count; ++i) {
        if (buf->lines.offset[i] > buf->cursor.cursor_pos) {
            break;
        }
        line = i;
    }

    buf->cursor.y = line;
    buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[line];
}

/**
 * Fungsi untuk Smart Auto Indent (Fixed Fallback Enter)
 */
void Syntax_auto_indent(Buffer *active_buf) {
    if (!active_buf || !active_buf->str) return;

    size_t pos = active_buf->cursor.cursor_pos;
    size_t len = active_buf->str->len;

    if (pos > len) pos = len;

    // Hitung depth dari Tree-Sitter jika ada
    int indent_depth = 0;
    if (active_buf->state && active_buf->state->tree) {
        TSTree *tree = active_buf->state->tree;
        TSNode root = ts_tree_root_node(tree);

        if (!ts_node_is_null(root)) {
            TSNode current = ts_node_descendant_for_byte_range(root, pos, pos);
            TSNode parent = current;
            while (!ts_node_is_null(parent)) {
                if (strcmp(ts_node_type(parent), "compound_statement") == 0) {
                    indent_depth++;
                }
                parent = ts_node_parent(parent);
            }
        }
    }
    // Jika Tree-Sitter NULL, indent_depth tetap 0 (Enter biasa tetep jalan!)

    Bytes prev_char = (pos > 0) ? String_get(active_buf->str, pos - 1, 1) : (Bytes){0};
    Bytes next_char = (pos < len) ? String_get(active_buf->str, pos, 1) : (Bytes){0};

    // Kasus Smart Enter pas di antara { dan }
    if (prev_char.data && next_char.data && prev_char.data[0] == '{' && next_char.data[0] == '}') {
        int current_line_indent = 0;
        size_t line_start = active_buf->lines.offset[active_buf->cursor.y];

        while (line_start + current_line_indent < pos) {
            Bytes b = String_get(active_buf->str, line_start + current_line_indent, 1);
            if (b.data && b.data[0] == ' ') {
                current_line_indent++;
                Bytes_free(&b);
            } else {
                Bytes_free(&b);
                break;
            }
        }

        int base_spaces = current_line_indent;
        int inner_spaces = current_line_indent + 4;

        if (base_spaces > 200) base_spaces = 200;
        if (inner_spaces > 200) inner_spaces = 200;

        char str1[256];
        str1[0] = '\n';
        memset(str1 + 1, ' ', inner_spaces);
        str1[inner_spaces + 1] = '\0';

        char str2[256];
        str2[0] = '\n';
        memset(str2 + 1, ' ', base_spaces);
        str2[base_spaces + 1] = '\0';

        Buffer_insert(active_buf, pos, str2);
        Buffer_insert(active_buf, pos, str1);

        active_buf->cursor.cursor_pos = pos + strlen(str1);
        sync_cursor_line_from_pos(active_buf);
        Bytes_free(&prev_char);
        Bytes_free(&next_char);
        return;
    }

    // Kasus Enter Biasa
    int space_to_add = indent_depth * 4;
    if (space_to_add > 200) space_to_add = 200;

    char insert_str[256];
    insert_str[0] = '\n';
    memset(insert_str + 1, ' ', space_to_add);
    insert_str[space_to_add + 1] = '\0';

    Buffer_insert(active_buf, pos, insert_str);

    active_buf->cursor.cursor_pos = pos + strlen(insert_str);
    sync_cursor_line_from_pos(active_buf);

    Bytes_free(&prev_char);
    Bytes_free(&next_char);
}
