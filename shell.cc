#include <cstdio>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "shell.hh"

int yyparse(void);

// 全局变量跟踪是否有命令正在运行
bool commandRunning = false;

bool Shell::promptNeeded = false;

// SIGINT信号处理函数
void sigintHandler(int sig) {
    // 如果没有命令运行，打印新提示符
    if (!commandRunning) {
        printf("\n");  // 清除当前行
        Shell::prompt();  // 显示新提示符
        fflush(stdout);
    }
    // 如果命令正在运行，什么都不做，让信号传递给子进程
}

// SIGCHLD信号处理函数
void sigchldHandler(int sig) {
    pid_t pid;
    int status;
    
    // 使用WNOHANG选项非阻塞等待任何子进程
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      if (Shell::isTerminal()) {
        printf("[%d] exited.\n", pid);
        Shell::promptNeeded = true;  // 标记需要显示提示符
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
    if (isatty(0)) {  // 只有当输入来自终端时才显示提示符
        printf("myshell>");
        fflush(stdout);
    }
}

int main() {
    // 设置SIGINT信号处理
    struct sigaction sa;
    sa.sa_handler = sigintHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // 避免系统调用被中断
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // 设置SIGCHLD信号处理
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchldHandler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;  // 避免系统调用被中断
    
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction for SIGCHLD");
        exit(1);
    }
    
    Shell::prompt();
    yyparse();
    return 0;
}

Command Shell::_currentCommand;
