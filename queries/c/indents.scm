; === C Indent Rules ===

; Increment indent di dalam compound statement / blok { }
(compound_statement) @indent
(field_declaration_list) @indent
(enumerator_list) @indent
(initializer_list) @indent

; Increment indent untuk statement percabangan / loop
(if_statement) @indent
(for_statement) @indent
(while_statement) @indent
(do_statement) @indent
(case_statement) @indent

; Decrement indent pada kurung tutup
"}" @outdent
")" @outdent
