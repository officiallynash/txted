/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef TYPES_H
#define TYPES_H

#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

/**
 * Forward Declaration dulu
 */
typedef struct String String;
typedef struct Clipboard Clipboard;
typedef struct SyntaxState SyntaxState;
typedef struct DiagnosticList DiagnosticList;
typedef struct FloatPrompt FloatPrompt;

// define max tab
constexpr int MAX_TABS = 7;
constexpr int MAX_FILE_GIT = 256;

// Karena float harus di deklarasikan dari awal
// Jadi ga bisa pakai constexpr,
// Ya kita tahu bahwa font size itu dinamis berdasarkan settings
constexpr size_t TAB_H = 36;
constexpr size_t STATUS_H = 26;
constexpr size_t DIAG_PANEL_H = 26;
constexpr size_t PAD_X = 16;
constexpr size_t PAD_Y = 10;
constexpr size_t LINE_H = 24;
constexpr size_t GUTTER_W = 50;

// Daftar semua Flag di Struct Buffer Manager
// Kita gunakan Bitwise untuk menghemat memory
// Selain itu untuk mempercepat Toggle ketika perpindahan Flag
constexpr uint8_t TXTED_REQ_EXIT = (1U << 0);
constexpr uint8_t TXTED_EXIT = (1U << 1);
constexpr uint8_t TXTED_SHOW_FM = (1U << 2);
constexpr uint8_t TXTED_SHOW_HELP = (1U << 3);
constexpr uint8_t TXTED_SHOW_GIT = (1U << 4);

// Deskripsi Bitwise operation untuk pengganti
// Flag yang memakai bool dari stdbool
// Walaupun agak "ribet" tapi ini untuk menghemat memori
constexpr uint8_t LSP_ENABLE = (1U << 0);
constexpr uint8_t LSP_VISIBLE = (1U << 1);
constexpr uint8_t LSP_REQUEST_PENDING = (1U << 2);
constexpr uint8_t LSP_HAS_COMP = (1U << 3);
constexpr uint8_t LSP_HAS_SIG = (1U << 4);
constexpr uint8_t LSP_SIG_PENDING = (1U << 5);
constexpr uint8_t LSP_HAS_HOVE = (1U << 6);
constexpr uint8_t LSP_HOV_PENDING = (1U << 7);
constexpr float LSP_DEBOUNCE_DELAY = 0.20f;

// Undo
constexpr int UNDO_MAX_ACTIONS = 1000;
constexpr int UNDO_TIMEOUT = 500;

// Flag untuk penanda Buf Flag
constexpr uint8_t BUF_IS_DIRTY = (1U << 0);
constexpr uint8_t BUF_IS_DRAGGING = (1U << 1);
constexpr uint8_t BUF_IS_SELECT = (1U << 2);

/**
 * Enum untuk penanda aksi insert atau delete
 */
typedef enum { UNDO_INSERT, UNDO_DELETE } UndoType;

/**
 * Struct untuk konfigurasi Undo
 */
typedef struct {
    UndoType type;
    size_t offset;
    size_t len;
    char *text;

    long timestamp_ms;
} UndoAction;

/**
 * Struct untuk Stack Undo
 */
typedef struct {
    UndoAction *actions;
    size_t count;
    size_t capacity;
    size_t current;
    bool is_undoing;
} UndoStack;

/**
 * Struct untuk membungkus ketika String_get
 */
typedef struct {
    unsigned char *data;
    size_t len;
} Bytes;

/**
 * Enum untuk Gutter Git
 */
typedef enum {
    GUTTER_NONE = 0,
    GUTTER_ADDED,     // Hijau (+)
    GUTTER_MODIFIED,  // Kuning (~)
    GUTTER_DELETED    // Merah (-)
} GutterStatus;

/**
 * Struct untuk menyimpan Line
 */
typedef struct {
    GutterStatus status;    // GUTTER_ADDED, GUTTER_MODIFIED
    double last_edited_at;  // Timestamp dari GetTime() Raylib saat baris di-edit
    char author[64];
} LineGitMeta;

/**
 * Struct untuk membungkus Position
 */
typedef struct {
    size_t x, y, cursor_pos;
} Position;

/**
 * Struct pembungkus untuk membungkus LineIndex mulai dari line count dan offset dari cursor pos
 * Digunakan untuk indexing cepat.
 */
typedef struct {
    size_t *offset, line_count, capacity;
} LineIndex;

/**
 * Struct pembungkus untuk Buffer Editor, Struct ini tier ke 2 setelah String (Rope)
 */
typedef struct {
    char *path;
    char *filename;
    char *language_id;
    String *str;
    SyntaxState *state;  // Tree-sitter
    int lsp_version;
    UndoStack undo;

    LineIndex lines;
    Position cursor;

    // Pengganti struct Selection
    union {
        size_t start;
    };
    int scroll_y;       // Ui State
    uint8_t buf_flags;  // Buffer flag penanda

    DiagnosticList *diagnostic;  // Diagnostic
    LineGitMeta *line_git;
    size_t meta_capacity;
} Buffer;

/**
 * Struct untuk menampung Search
 */
typedef struct {
    char label[256];
    size_t line, col;
} SearchHitBuffer;

/**
 * Enum sebagai penanda Focus Mode
 * Jadi untuk bitwise nantinya hanya untuk simpan flags
 */
typedef enum {
    FILE_MANAGER,
    WRITE,
    POPUP,
} FocusMode;

/**
 * Enum untuk Switch Tab
 */
typedef enum SwitchTab { PREV, NEXT } SwitchTab;

/**
 * Struct pembungkus untuk Buffer, ini jantungnya Multi tab
 */
typedef struct BufManager {
    Buffer *buf[MAX_TABS];  // Array buffer
    size_t num_tabs;        // Untuk num tabs
    int active_idx;         // Active idx
    Clipboard *clp;         // Clipboard
    float fm_width_ratio;   // Ratio untuk File Manager
    char *path_root;        // Menyimpan path root, untuk kebutuhan workspace
    uint8_t win_flags;      // Flag untuk menampung state window, misal minta exit, dll
    FocusMode mode;
    FloatPrompt *prompt;
} BufManager;

/**
 * Struct untuk state dari Tree-Sitter (Yang disimpan di Buffer)
 */
struct SyntaxState {
    TSParser *parser;
    TSTree *tree;
    TSQuery *query;
    TSQuery *indents_query;
    bool is_enabled;
};

/**
 * Struct untuk data Config LSP dan Tree-sitter saat Open File
 */
typedef struct {
    char **lsp_args;
    char *path_lsp;
    char *query_source;
    char *indent_source;
    const char *language_id;
    const TSLanguage *lang;
} LangConfig;

/**
 * Struct untuk menampung HighlightToken
 */
typedef struct {
    const char *capture_name;  // "keyword", "string", "function", dll.
    uint32_t start_byte;
    uint32_t end_byte;
} HighlightToken;

// Hover
typedef struct {
    char *contents;  // markdown / plain text
    int start_line;  // range opsional
    int start_char;
    int end_line;
    int end_char;
    bool has_range;
} HoverInfo;

/*
 * Signature Help / Parameter Hints
 */
typedef struct {
    char *documentation;
    int start;
    int end;
} ParameterInfo;

/*
 * Signature Help Item
 */
typedef struct {
    char *label;
    char *documentation;
    int active_parameter;

    ParameterInfo *parameters;
    size_t parameter_count;
} SignatureItem;

/*
 * Signature Help
 */
typedef struct {
    SignatureItem *items;
    size_t count;
    int active_signature;
} SignatureHelp;

/*
 * Diagnostic Level
 */
typedef enum {
    LSP_SEVERITY_ERROR = 1,
    LSP_SEVERITY_WARNING = 2,
    LSP_SEVERITY_INFO = 3,
    LSP_SEVERITY_HINT = 4
} DiagnosticSeverity;

/**
 * Diagnostic Item
 */
typedef struct {
    char *message;
    char *source;
    int start_line;
    int start_char;
    int end_line;
    int end_char;
    int severity;
} DiagnosticItem;

/*
 * Diagnostic List
 */
struct DiagnosticList {
    char uri[512];
    DiagnosticItem *items;
    size_t count;
};

/*
 * Completion Item
 */
typedef struct {
    char *label;        // yang ditampilkan
    char *insert_text;  // yang di-insert (bisa NULL → pakai label)
    char *detail;       // tipe / info tambahan (bisa NULL)
    char *header_include;
} CompletionItem;

/*
 * Completion List
 */
typedef struct {
    CompletionItem *items;
    size_t count;
} CompletionList;

/*
 * Text Edit untuk Auto Format
 */
typedef struct {
    char *new_text;
    int start_line;
    int start_char;
    int end_line;
    int end_char;
} TextEdit;

/*
 * Text Edit list auto format
 */
typedef struct {
    TextEdit *edits;
    size_t count;
} TextEditList;

/**
 * Struct penampung hasil dari Completion yang sudah di filter
 */
typedef struct {
    CompletionItem *item;
    int score;
} FilteredItem;

/**
 * Enum penanda Completion dan Signature
 * digunakan untuk auto deteksi ketika rendering
 * agar tidak tunmpang tindih
 */
typedef enum { POPUP_BELOW, POPUP_ABOVE } PopupSide;

/**
 * Struct utama untuk mengatur LSP state
 */
typedef struct {
    char *root_uri;
    char uri[512];
    char language_id[32];
    char current_text[8192];

    int selected_index;
    int last_line;
    int last_character;
    uint8_t lsp_flag;

    // Completion
    CompletionList completion;
    PopupSide completion_side;
    PopupSide signature_side;

    // Signature
    SignatureHelp signature_help;
    size_t sig_y;

    // Hover
    HoverInfo hover;
    float hover_scroll;

} LspUiState;

/**
 * Struct untuk menampung Prompt Buffer
 */
typedef struct {
    String *str;
    size_t len;  // Kasih manual di struct agar lebih efisien
    size_t cursor_pos;
} PromptBuffer;

/**
 * Struct untuk membawa data dari Prompt
 */
typedef struct {
    char label[128];
    char subtext[128];
    int icon_id;
} PromptItem;

/**
 * Enum untuk PromptType, aku rasa lebih efisien memakai Enum daripada Bitwise Flag.
 */
typedef enum {
    PROMPT_TYPE_SAVE,
    PROMPT_SAVE_AS,
    PROMPT_TYPE_OPEN_FILE,
    PROMPT_TYPE_NEW_FILE,
    PROMPT_TYPE_NEW_FOLDER,
    PROMPT_TYPE_SEARCH
} PromptType;

/**
 * Struct untuk konfigurasi FloatPrompt
 */
struct FloatPrompt {
    char label[64];
    int icon_id;

    int selected_idx;
    int scroll_offset;
    bool is_active;
    bool edit_mode;
    PromptType type;
    PromptBuffer *prb;
};

/**
 * Struct untuk File List (Fuzzi search)
 */
typedef struct {
    PromptItem *items;
    size_t item_count;
    size_t capacity;
} FileList;

/**
 * Struct untuk membawa data ke Buffer
 */
typedef struct {
    char *full_path;
    uint8_t *data;
    size_t size;
    bool is_success;
} FileData;

/**
 * Struct untuk menampung Status dari File
 */
typedef struct {
    char path[512];
    char mark[4];
} GitFileStatus;

/**
 * Struct untuk menampung Git
 */
typedef struct {
    char author[64];
    char branch[128];
    bool is_repo;
    bool has_changes;
    int modified;
    int untracked;

    // File status
    GitFileStatus files[MAX_FILE_GIT];
    int file_count;
} GitStatus;

/**
 * Struct untuk GitPopup
 */
typedef struct {
    char message[256];
    char last_error[256];
    int selected;
    float list_scroll;
    bool edit_message;
} GitPopup;

/**
 * Struct untuk Layouting
 */
typedef struct {
    int win_h, win_w;
    int fm_x, fm_y, fm_w, fm_h;  // area file manager
    int editor_x, editor_y;      // origin editor
    int editor_w, editor_h;      // ukuran editor
    int gutter_screen_x;         // X gutter di layar
    int text_screen_x;           // X awal teks di layar
    int visible_lines;           // Visible Lines
} EditorLayout;

/**
 * Enum penanda posisi untuk File Manager
 */
typedef enum { FM_LEFT, FM_RIGHT } Settings_fm;

/**
 * Struct untuk data settings aplikasi
 */
typedef struct {
    char theme[64];
    char font[128];
    int font_size;

    Settings_fm fm_pos;
} Settings;

/**
 * Struct untuk konfigurasi theme
 */
typedef struct {
    // Canvas / Window
    Color bg_main;     // Background editor utama
    Color bg_sidebar;  // Sidebar / Panel
    Color bg_editor;
    Color bg_card;  // Modal / Pop-up card (LSP, Prompt)
    Color border;   // Line/Border komponen UI
    Color active_tab;
    Color active_line;
    Color line_active;
    Color line_num;
    Color backdrop;  // Transparan

    // Status & Feedback
    Color selection;  // Highlight teks / item terpilih
    Color cursor;     // Warna kursor
    Color accent;     // Warna aksen/fokus utama

    // Typography (Teks)
    Color text_normal;     // Teks biasa
    Color text_muted;      // Teks redup (sub-label, placeholder)
    Color text_highlight;  // Teks saat di-hover/select

    // Syntax
    Color keyword;
    Color type;
    Color string;
    Color function;
    Color number;
    Color comment;
    Color method;
    Color operator;
    Color constant;

    Color bracket_match;
    Color bracket_match_bg;
    // Notification / Status Colors
    Color success;
    Color warning;
    Color error;
    Color info;
} UITheme;

#endif
