grammar Calc;

// A combined grammar: left recursion, literals in parser rules, a fragment, a skipped token and a non-greedy comment.

prog : stat+ ;

stat : expr NEWLINE
     | ID '=' expr NEWLINE
     | NEWLINE
     ;

expr : expr ('*' | '/') expr
     | expr ('+' | '-') expr
     | INT
     | ID
     | '(' expr ')'
     ;

ID      : LETTER+ ;
INT     : DIGIT+ ;
NEWLINE : '\r'? '\n' ;
WS      : [ \t]+ -> skip ;
COMMENT : '/*' .*? '*/' -> skip ;
fragment LETTER : [a-zA-Z] ;
fragment DIGIT  : [0-9] ;
