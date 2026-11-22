%option noyywrap
DIGIT   [0-9]
LETTER  [a-zA-Z_]

%%

[ \t]+                          ;
"#".*                           ;
"print"                         { return KW_PRINT; }
{DIGIT}+("."{DIGIT}+)?          { yylval = atof(yytext); return NUMBER; }
{LETTER}({LETTER}|{DIGIT})*     { return IDENT; }
\n                              { return NEWLINE; }
.                               { return yytext[0]; }

%%
