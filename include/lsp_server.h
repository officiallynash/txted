#ifndef LSP_COMPLETION_H
#define LSP_COMPLETION_H

#include <stdbool.h>
#include <stddef.h>

// Hover
typedef struct {
    char *contents;  // markdown / plain text
    int start_line;  // range opsional
    int start_char;
    int end_line;
    int end_char;
    bool has_range;
} HoverInfo;

// Signature Help / Parameter Hints
typedef struct {
    int start;
    int end;
    char *documentation;
} ParameterInfo;

typedef struct {
    char *label;
    char *documentation;
    int active_parameter;

    ParameterInfo *parameters;
    size_t parameter_count;
} SignatureItem;

typedef struct {
    SignatureItem *items;
    size_t count;
    int active_signature;
} SignatureHelp;

// Diagnostic
typedef enum {
    LSP_SEVERITY_ERROR = 1,
    LSP_SEVERITY_WARNING = 2,
    LSP_SEVERITY_INFO = 3,
    LSP_SEVERITY_HINT = 4
} DiagnosticSeverity;

typedef struct {
    int start_line;
    int start_char;
    int end_line;
    int end_char;
    int severity;
    char *message;
    char *source;
} DiagnosticItem;

typedef struct {
    char uri[512];
    DiagnosticItem *items;
    size_t count;
} DiagnosticList;

// Completion
typedef struct {
    char *label;        // yang ditampilkan
    char *insert_text;  // yang di-insert (bisa NULL → pakai label)
    char *detail;       // tipe / info tambahan (bisa NULL)
    char *header_include;
} CompletionItem;

typedef struct {
    CompletionItem *items;
    size_t count;
} CompletionList;

// Auto Format
typedef struct {
    int start_line;
    int start_char;
    int end_line;
    int end_char;
    char *new_text;
} TextEdit;

typedef struct {
    TextEdit *edits;
    size_t count;
} TextEditList;

// Mulai clangd + handshake
bool lsp_start(const char *lsp_path, char **argv, const char *workspace_root);
void lsp_did_open(const char *uri, const char *language_id, const char *text);
void lsp_did_change(const char *uri, const char *text, int version);
void lsp_did_close(const char *uri);
void lsp_stop(void);

// Completion
CompletionList lsp_completion(const char *uri, int line, int character, char trigger_char);
void lsp_free_completion(CompletionList *list);

// Diagnostic
DiagnosticList *lsp_get_diagnostics(const char *uri);
void lsp_free_diagnostics(DiagnosticList *list);

// Signature Help
SignatureHelp lsp_signature_help(const char *uri, int line, int character);
void lsp_free_signature_help(SignatureHelp *help);

// Auto format
TextEditList lsp_format(const char *uri, int tab_size, bool insert_spaces);
void lsp_free_text_edits(TextEditList *list);

// Hover
HoverInfo lsp_hover(const char *uri, int line, int character);
void lsp_free_hover(HoverInfo *hover);
#endif
