; === Go Indent Rules ===

; Increment indent di dalam block { }
(block) @indent
(literal_value) @indent
(struct_type) @indent
(interface_type) @indent

; Increment indent untuk switch/select case & komunikasi chan
(expression_case_clause) @indent
(type_case_clause) @indent
(communication_case_clause) @indent

; Increment indent untuk parameter multiline
(parameter_list) @indent
(argument_list) @indent

; Decrement indent pada kurung tutup
"}" @outdent
")" @outdent
"]" @outdent
