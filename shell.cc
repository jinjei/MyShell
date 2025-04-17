#include <cstdio>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include "shell.hh"

int yyparse(void);

// global variable: track Whether a command is running
bool commandRunning = false; //false: no foreground process, shell is idle

bool Shell::promptNeeded = false;
bool Shell::_isTerminal = false;  // 添加静态成员初始化
std::string Shell::_shellPath = "";  // Shell路径初始化

// SIGINT handle function (CtrlC)
void sigintHandler(int sig) {
    // If no command is running, print a new prompt
    if (!commandRunning) {
        printf("\n");  // clear current line
        Shell::prompt();  // new prompt
        fflush(stdout);
    }
    // If the command is running, do nothing
    //  let the signal pass to the child process
}

// SIGCHLD handle function (zombie elimination)
void sigchldHandler(int sig) {
    pid_t pid;
    int status;
    
    // 使用WNOHANG选项非阻塞等待任何子进程
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      if (Shell::isTerminal()) {
        printf("[%d] exited.\n", pid);
        Shell::promptNeeded = true;  // new prompt
        fflush(stdout);
      }
    }
    
    if (Shell::promptNeeded) {
      Shell::prompt();
      Shell::promptNeeded = false;
    }
}

bool Shell::isTerminal() {
    return isatty(0);
}

void Shell::prompt() {
    if (isatty(0)) {  // print prompt only if input coming from terminal
        _isTerminal = true;
        printf("myshell>");
        fflush(stdout);
    } else {
        _isTerminal = false;
    }
}

int main(int argc, char **argv) {
    // Save the filepath to the shell ${SHELL}
    Shell::_shellPath = argv[0];
    
    // SIGINT signal handler(Ctrl-C)
    struct sigaction sa;
    sa.sa_handler = sigintHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // SIGCHLD signal handeler(zombie elimination)
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchldHandler;  //Assign handler
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = 0;  
    
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction for SIGCHLD");
        exit(1);
    }
    
    Shell::prompt();
    yyparse();
    return 0;
}

Command Shell::_currentCommand;
