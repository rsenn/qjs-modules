//%token   WORD
//%token   ASSIGNMENT_WORD
%token   NAME
%token   NEWLINE
%token   IO_NUMBER

/*       The following are the operators mentioned above.        */

%token   AND_IF OR_IF DSEMI
/*       '&&'   '||'  ';;'                                        */

%token   DLESS DGREAT LESSAND GREATAND LESSGREAT DLESSDASH
/*       '<<'  '>>'   '<&'    '>&'     '<>'     '<<-'            */

%token   CLOBBER
/*       '>|'                                                    */

/*       The following are the reserved words                    */

%token    IF   THEN   ELSE   ELIF   FI   DO   DONE
/*       'if' 'then' 'else' 'elif' 'fi' 'do' 'done'              */

%token    CASE   ESAC   WHILE   UNTIL   FOR
/*       'case' 'esac' 'while' 'until' 'for'                     */

/*       These are reserved words, not operator tokens, and are
        recognized when reserved words are recognized.          */

%token   LBRACE RBRACE BANG
/*        '{'    '}'    '!'                                      */

%token   IN
/*       'in'                                                    */

/*       -------------------------------------------------------
        The Grammar
        ------------------------------------------------------- */

%start   complete_command

%%

complete_command    : list separator
                    | list
                    ;

list                : list separator_op and_or
                    |                   and_or
                    ;

and_or              :                         pipeline
                    | and_or AND_IF linebreak pipeline
                    | and_or OR_IF  linebreak pipeline
                    ;

pipeline            :      pipe_sequence
                    | BANG pipe_sequence
                    ;

pipe_sequence       :                            command
                    | pipe_sequence PIPE linebreak command
                    ;

command             : simple_command
                    | compound_command
                    | compound_command redirect_list
                    | function_definition
                    ;

compound_command    : brace_group
                    | subshell
                    | for_clause
                    | case_clause
                    | if_clause
                    | while_clause
                    | until_clause
                    ;

subshell            : LPAREN compound_list RPAREN
                    ;

compound_list       :              term
                    | newline_list term
                    |              term separator
                    | newline_list term separator
                    ;

term                : term separator and_or
                    |                and_or
                    ;

for_clause          : FOR name                            do_group
                    | FOR name IN wordlist sequential_sep do_group
                    ;

name                : NAME                     /* Apply rule (5) */
                    ;

in                  : IN                       /* Apply rule (6) */
                    ;

wordlist            : wordlist WORD
                    |          WORD
                    ;

case_clause         : CASE WORD IN linebreak case_list ESAC
                    | CASE WORD IN linebreak           ESAC
                    ;

case_list           : case_list case_item
                    |           case_item
                    ;

case_item           :     pattern RPAREN linebreak     DSEMI linebreak
                    |     pattern RPAREN compound_list DSEMI linebreak
                    | LPAREN pattern RPAREN linebreak     DSEMI linebreak
                    | LPAREN pattern RPAREN compound_list DSEMI linebreak
                    ;

pattern             :            WORD          /* Apply rule (4) */
                    | pattern PIPE WORD         /* DO not apply rule (4) */
                    ;

if_clause           : IF compound_list THEN compound_list else_part FI
                    | IF compound_list THEN compound_list           FI
                    ;

else_part           : ELIF compound_list THEN else_part
                    | ELSE compound_list
                    ;

while_clause        : WHILE compound_list do_group
                    ;

until_clause        : UNTIL compound_list do_group
                    ;

function_definition : fname LPAREN RPAREN linebreak function_body
                    ;

function_body       : compound_command                /* Apply rule (9) */
                    | compound_command redirect_list  /* Apply rule (9) */
                    ;

fname               : NAME                            /* Apply rule (8) */
                    ;

brace_group         : LBRACE compound_list RBRACE
                    ;

do_group            : DO compound_list DONE
                    ;

simple_command      : cmd_prefix cmd_word cmd_suffix
                    | cmd_prefix cmd_word
                    | cmd_prefix
                    | cmd_name cmd_suffix
                    | cmd_name
                    ;

cmd_name            : WORD                   /* Apply rule (7)(a) */
                    ;

cmd_word            : WORD                   /* Apply rule (7)(b) */
                    ;

cmd_prefix          :            io_redirect
                    | cmd_prefix io_redirect
                    |            ASSIGNMENT_WORD
                    | cmd_prefix ASSIGNMENT_WORD
                    ;

cmd_suffix          :            io_redirect
                    | cmd_suffix io_redirect
                    |            WORD
                    | cmd_suffix WORD
                    ;


redirect_list       :               io_redirect
                    | redirect_list io_redirect
                    ;

io_redirect         :           io_file
                    | IO_NUMBER io_file
                    |           io_here
                    | IO_NUMBER io_here
                    ;

io_file             : LESS      filename
                    | LESSAND   filename
                    | GREAT     filename
                    | GREATAND  filename
                    | DGREAT    filename
                    | LESSGREAT filename
                    | CLOBBER   filename
                    ;

filename            : WORD                      /* Apply rule (2) */
                    ;

io_here             : DLESS     here_end
                    | DLESSDASH here_end
                    ;

here_end            : WORD                      /* Apply rule (3) */
                    ;

newline_list        :              NEWLINE
                    | newline_list NEWLINE
                    ;

linebreak           : newline_list
                    | /* empty */
                    ;

separator_op        : BACKGND
                    | SEMI
                    ;

separator           : separator_op linebreak
                    | newline_list
                    ;

sequential_sep      : SEMI linebreak
                    | newline_list
                    ;
