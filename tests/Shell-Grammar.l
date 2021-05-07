D   [0-9]
L   [a-zA-Z_]
A   [a-zA-Z_0-9]
NL  \n
WS  [ \t\v\n\f]

%%
"#".*                                    { /* consume #-comment */ }

{NL}                 { return NEWLINE; }
";"                  { return SEMI; }
";;"                 { return DSEMI; }
"&"                  { return BACKGND; }
"&&"                 { return AND_IF; }
"|"                  { return PIPE; }
"||"                 { return OR_IF; }
"("                  { return LPAREN; }
")"                  { return RPAREN; }
"`"                  { return BQ; }

"<<"                 { return DLESS; }
"<"                  { return LESS; }
">>"                 { return DGREAT; }
">"                  { return GREAT; }
"<&"                 { return LESSAND; }
">&"                 { return GREATAND; }
"<>"                 { return LESSGREAT; }
"<<-"                { return DLESSDASH; }
">|"                 { return CLOBBER; }

{D}+(?=[<>])         { return IO_NUMBER; }

{L}{A}+              { return NAME; }

"WORD"               { return WORD; }
"ASSIGN"             { return ASSIGN; }
"REDIR"              { return REDIR; }

"!"                  { return BANG; }
"case"               { return CASE; }
"do"                 { return DO; }
"done"               { return DONE; }
"elif"               { return ELIF; }
"else"               { return ELSE; }
"esac"               { return ESAC; }
"fi"                 { return FI; }
"for"                { return FOR; }
"if"                 { return IF; }
"in"                 { return IN; }
"then"               { return THEN; }
"until"              { return UNTIL; }
"while"              { return WHILE; }
"{"                  { return LBRACE; }
"}"                  { return RBRACE; }


{WS}+                { /* whitespace separates tokens */ }
.                    { /* discard bad characters */ }

%%
