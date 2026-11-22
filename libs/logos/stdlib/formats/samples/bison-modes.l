%option noyywrap case-insensitive
%x STRING COMMENT
DEC     [0-9]+
HEX     0x[0-9a-f]{1,8}
WORD    [[:alpha:]_][[:alnum:]_]*

%%

"/*"                    BEGIN(COMMENT);
<COMMENT>"*/"           BEGIN(INITIAL);
<COMMENT>[^*]+|"*"      ;
"//".*                  ;

\"                      { BEGIN(STRING); }
<STRING>\"              { BEGIN(INITIAL); return STR; }
<STRING>\\.|[^"\\\n]+   { /* the text of the string */ }

true |
yes                     { return TRUE; }
false                   return FALSE;
{HEX}|{DEC}             return NUM;
{WORD}                  return ID;
[ \t\r\n]+              ;
[[:punct:]]             return yytext[0];

%%
