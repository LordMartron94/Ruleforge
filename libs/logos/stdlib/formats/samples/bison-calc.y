/* A calculator in the style of the Bison manual: tokens with types, operator precedence and associativity, a unary minus with %prec,
   a non-associative comparison, mid-rule and end-of-rule actions, error recovery, and a rule list written with and without semicolons. */
%{
#include <stdio.h>
#include <math.h>
int yylex(void);
void yyerror(const char *message);
%}

%define api.value.type {double}
%expect 0
%token <num> NUMBER
%token <name> IDENT
%token NEWLINE
%token KW_PRINT "print"

%right '='
%nonassoc '<' '>'
%left '+' '-'
%left '*' '/'
%precedence NEG
%right '^'

%start session

%%

session
  : %empty
  | session line
  ;

line
  : NEWLINE
  | statement NEWLINE
  | error NEWLINE            { yyerrok; }
  ;

statement
  : expr                     { printf("%g\n", $1); }
  | "print" expr
  | IDENT '=' expr
  ;

expr
  : NUMBER
  | IDENT
  | expr '+' expr            { $$ = $1 + $3; }
  | expr '-' expr
  | expr '*' expr
  | expr '/' expr
  | expr '<' expr
  | expr '>' expr
  | '-' expr %prec NEG       { $$ = -$2; }
  | expr '^' expr
  | '(' { puts("open"); } expr ')'
  ;

%%

void yyerror(const char *message) { fprintf(stderr, "%s\n", message); }
