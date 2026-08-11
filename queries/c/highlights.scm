; --- Types ---
(primitive_type) @type
(type_identifier) @type

; --- Keywords & Modifiers ---
["return" "if" "else" "while" "for" "do" "switch" "case" "default" "break" "continue"] @keyword
["struct" "union" "enum" "typedef" "sizeof" "static" "const" "volatile" "extern" "inline" "register" "auto" "signed" "unsigned" "short" "long"] @keyword

; --- Preprocessor ---
"#include" @keyword
"#define" @keyword
"#ifdef" @keyword
"#ifndef" @keyword
"#endif" @keyword
(preproc_directive) @keyword

; --- Literals ---
(number_literal) @number
(string_literal) @string
(char_literal) @string
(system_lib_string) @string

; --- Functions ---
(call_expression function: (identifier) @function)
(function_declarator declarator: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function))

; --- Struct / Union Members ---
(field_identifier) @property

; --- Comments ---
(comment) @comment
