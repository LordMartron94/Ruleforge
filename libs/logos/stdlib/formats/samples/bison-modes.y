/* A settings file: assignments of numbers, strings, booleans and lists. The scanner is the interesting part. */
%token ID NUM STR TRUE FALSE
%start file

%%

file  : %empty
      | file entry
      ;

entry : ID '=' value ';'
      ;

value : NUM
      | STR
      | TRUE
      | FALSE
      | '[' list ']'
      | '[' ']'
      ;

list  : value
      | list ',' value
      ;
