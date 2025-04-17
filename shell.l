/*
 * shell.l: lexical analyzer for shell
 */

%{
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "y.tab.hh"
#include "shell.hh"

static void yyunput(int c, char *buf_ptr);

void myunputc(int c) {
  unput(c);
}

std::string executeSubShellCommand(const char *command) {
    int pin[2], pout[2];
    
    // create two pipes
    if (pipe(pin) == -1 || pipe(pout) == -1) {
        perror("pipe");
        return "";
    }
    
    // create child process
    pid_t pid = fork();
    
    if (pid == 0) {
        // In child process
        
        // child reads from pin[0]
        dup2(pin[0], 0);
        
        // child writes to pout[1]
        dup2(pout[1], 1);
        
        close(pin[0]);
        close(pin[1]);
        close(pout[0]);
        close(pout[1]);
        
        // exccute myshell
        char *args[2];
        args[0] = (char *)Shell::_shellPath.c_str();
        args[1] = NULL;
        execvp(args[0], args);
        
        perror("execvp");
        _exit(1);
    } else if (pid < 0) {
        // fork error
        perror("fork");
        close(pin[0]);
        close(pin[1]);
        close(pout[0]);
        close(pout[1]);
        return "";
    }
    
    // In parent process
    close(pin[0]);
    close(pout[1]);
    
    // writes command to child
    std::string cmd = std::string(command) + "\nexit\n";
    write(pin[1], cmd.c_str(), cmd.length());
    close(pin[1]);  
    
    // reads output from child 
    std::string result;
    char buffer[4096];
    ssize_t bytes_read;
    
    while ((bytes_read = read(pout[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        result += buffer;
    }
    
    close(pout[0]); 
    
    // Wait for the child process to finish
    int status;
    waitpid(pid, &status, 0);
    
    // removing prompts and redundant '\n'
    size_t pos = 0;
    while ((pos = result.find("myshell>", pos)) != std::string::npos) {
        result.erase(pos, 8);  // remove "myshell>"
    }
    
    // replace '\n' to space
    for (size_t i = 0; i < result.length(); i++) {
        if (result[i] == '\n') {
            result[i] = ' ';
        }
    }
    
    pos = result.find("exit");
    if (pos != std::string::npos) {
        result = result.substr(0, pos);
    }
    
    // remove space at the end
    while (!result.empty() && isspace(result.back())) {
        result.pop_back();
    }
    
    return result;
}

%}

%option noyywrap

%%

\n {
  return NEWLINE;
}

[ \t] {
  /* Discard spaces and tabs */
}

">" {
  return GREAT;
}

"|" {
  return PIPE;
}

"<" {
  return LESS;
}

"2>" {
  return TWOGREAT;
}

">&" {
  return GREATAMPERSAND;
}

">>" {
  return GREATGREAT;
}

">>&" {
  return GREATGREATAMPERSAND;
}

"&" {
  return AMPERSAND;
}

"exit" {
  return EXIT;
}

"$("[^)]*")" {
  // Subshell   $(command)
  // remove $( and ) , get the command text
  std::string cmdText(yytext + 2, strlen(yytext) - 3);
  
  // execute command
  std::string output = executeSubShellCommand(cmdText.c_str());
  
  // Press output characters back into buffer from the back forward
  for (int i = output.length() - 1; i >= 0; i--) {
    unput(output[i]);
  }
}

["][^\n\"]*["] {
  /* Handle quoted strings - Remove the start and end quotes */
  yylval.cpp_string = new std::string(yytext);
  *yylval.cpp_string = yylval.cpp_string->substr(1, yylval.cpp_string->length()-2);
  return WORD;
}

[^ \t\n\>\<\|&]*\\[^ \t\n]* {
  /* Escape */
  char *str = strdup(yytext);
  char *newstr = (char*) malloc(strlen(str) + 1);
  
  int i = 0, j = 0;
  while (str[i]) {
    if (str[i] == '\\') {
      if (str[i+1] == '\\') {
        // 2 consecutive backslashes, test file
        newstr[j++] = '\\';
        i += 3; // skip 2 '\'
      } else if (str[i+1]) {
        //single backslash followed by other characters  
        // keeps escaped characters
        newstr[j++] = str[i+1];
        i += 2; // Skip '\' and escaped characters
      } else {
        newstr[j++] = '\\';
        i++;
      }
    } else {
      // normal characters
      newstr[j++] = str[i++];
    }
  }
  newstr[j] = '\0';
  
  yylval.cpp_string = new std::string(newstr);
  free(str);
  free(newstr);
  return WORD;
}

[^ \t\n\>\<\|&\\\"]+  {
  /* any normal word */
  yylval.cpp_string = new std::string(yytext);
  return WORD;
}

%%
