;; 1. Keywords
[
  "break" "case" "chan" "const" "continue"
  "default" "defer" "else" "fallthrough" "for"
  "func" "go" "goto" "if" "import"
  "interface" "map" "package" "range" "return"
  "select" "struct" "switch" "type" "var"
] @keyword

;; 2. Builtin Constants & Booleans
(nil) @constant.builtin
[
  (true)
  (false)
] @boolean

;; 3. Types & Modules / Packages
(type_identifier) @type
(package_identifier) @module

;; 4. Functions & Methods
(function_declaration name: (identifier) @function)
(method_declaration name: (field_identifier) @function)
(call_expression function: (identifier) @function)
(call_expression function: (selector_expression field: (field_identifier) @function))

;; 5. Strings & Literals
(interpreted_string_literal) @string
(raw_string_literal) @string
(rune_literal) @string
(int_literal) @number
(float_literal) @number
(comment) @comment

;; 6. Operators
[
  "+" "-" "*" "/" "%" "&" "|" "^" "<<" ">>" "&^"
  "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "<<=" ">>=" "&^="
  "&&" "||" "<-" "++" "--" "==" "<" ">" "=" "!"
  "!=" "<=" ">=" ":=" "..."
] @operator

;; 7. Delimiters & Brackets
[
  "(" ")" "[" "]" "{" "}"
] @punctuation.bracket

[
  "." "," ";" ":"
] @punctuation.delimiter

;; 8. Fields & Variables (Ditaruh Paling Bawah Sebagai Fallback)
(field_declaration name: (field_identifier) @property)
(field_identifier) @property
(identifier) @variable