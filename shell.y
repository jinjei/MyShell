%code requires 
{
#include <string>

#if __cplusplus > 199711L
#define register      // Deprecated in C++11 so remove the keyword
#endif
}

%union
{
  char        *string_val;
  // Example of using a c++ type in yacc
  std::string *cpp_string;
}

%token <cpp_string> WORD
%token NOTOKEN GREAT NEWLINE PIPE LESS TWOGREAT GREATAMPERSAND GREATGREAT GREATGREATAMPERSAND AMPERSAND EXIT

%{
#include <stdio.h>
#include "shell.hh"

void yyerror(const char * s);
int yylex();

%}

%%

GOAL:
  Commands
  ;

Commands:
  Command
  | Command Commands
  ;

Command: simple_command
       ;

simple_command:	
  exit_command
  |pipe_list iomodifier_list background_opt NEWLINE {
    //printf(" Yacc: Execute command\n");
    Shell::_currentCommand.execute();
  }
  | NEWLINE 
  | error NEWLINE { yyerrok; }
  ;

exit_command:
  EXIT NEWLINE {
    if(Shell::isTerminal()) {
      printf("Good bye!!\n");
    }
    
    exit(0);
  }

pipe_list:
  command_and_args
  | pipe_list PIPE command_and_args
  ;

command_and_args:
  command_word argument_list {
    Shell::_currentCommand.
    insertSimpleCommand( Command::_currentSimpleCommand );
  }
  ;

argument_list:
  argument_list argument
  | /* can be empty */
  ;

argument:
  WORD {
    //printf(" Yacc: insert argument \"%s\"\n", $1->c_str());
    Command::_currentSimpleCommand->insertArgument( $1 );
  }
  ;

command_word:
  WORD {
    //printf(" Yacc: insert command \"%s\"\n", $1->c_str());
    Command::_currentSimpleCommand = new SimpleCommand();
    Command::_currentSimpleCommand->insertArgument( $1 );
  }
  ;

iomodifier_list:
  iomodifier_list iomodifier
  | /* can be empty */
  ;

iomodifier:
  GREAT WORD {
    if (Shell::_currentCommand._outFile) {
      fprintf(stderr, "Ambiguous output redirect.\n");
      Shell::_currentCommand._redirectError = true;  //error flag for multiple redirect
    } else {
      //printf(" Yacc: insert output \"%s\"\n", $2->c_str());
      Shell::_currentCommand._outFile = $2;
    }
  }
  | LESS WORD {
    if (Shell::_currentCommand._inFile) {
      fprintf(stderr, "Ambiguous input redirect.\n");
      Shell::_currentCommand._redirectError = true;  //error flag for multiple redirect
    } else {
      //printf(" Yacc: insert input \"%s\"\n", $2->c_str());
      Shell::_currentCommand._inFile = $2;
    }
  }
  | TWOGREAT WORD {
    if (Shell::_currentCommand._errFile) {
      fprintf(stderr, "Ambiguous error redirect.\n");
      Shell::_currentCommand._redirectError = true;
    } else {
      //printf(" Yacc: insert error \"%s\"\n", $2->c_str());
      Shell::_currentCommand._errFile = $2;
    }
  }
  | GREATAMPERSAND WORD {
    if (Shell::_currentCommand._outFile || Shell::_currentCommand._errFile) {
      fprintf(stderr, "Ambiguous output/error redirect.\n");
      Shell::_currentCommand._redirectError = true;
    } else {
      //printf(" Yacc: insert output and error \"%s\"\n", $2->c_str());
      Shell::_currentCommand._outFile = $2;
      Shell::_currentCommand._errFile = $2;
    }
  }
  | GREATGREAT WORD {
    if (Shell::_currentCommand._outFile) {
      fprintf(stderr, "Ambiguous output redirect.\n");
      Shell::_currentCommand._redirectError = true;
    } else {
      //printf(" Yacc: append output \"%s\"\n", $2->c_str());
      Shell::_currentCommand._outFile = $2;
      Shell::_currentCommand._appendOut = true;
    }
  }
  | GREATGREATAMPERSAND WORD {
    if (Shell::_currentCommand._outFile || Shell::_currentCommand._errFile) {
      fprintf(stderr, "Ambiguous output/error redirect.\n");
      Shell::_currentCommand._redirectError = true;
    } else {
      //printf(" Yacc: append output and error \"%s\"\n", $2->c_str());
      Shell::_currentCommand._outFile = $2;
      Shell::_currentCommand._errFile = $2;
      Shell::_currentCommand._appendOut = true;
      Shell::_currentCommand._appendErr = true;
    }
  }
  ;

background_opt:
  AMPERSAND {
    Shell::_currentCommand._background = true;
  }
  | /* can be empty */
  ;

%%

void
yyerror(const char * s)
{
  fprintf(stderr,"%s", s);
}

#if 0
main()
{
  yyparse();
}
#endif