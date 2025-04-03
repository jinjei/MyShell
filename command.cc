#include <cstdio>
#include <cstdlib>
#include <unistd.h>     //对于dup, dup2, pipe等系统调用
#include <wait.h>       //waitpid系统调用
#include <sys/types.h>  //pid_t
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <limits.h>     // PATH_MAX
#include <sstream>      // stringstream
#include <string>
#include <vector>

#include "command.hh"
#include "shell.hh"
#include "y.tab.hh"     // 需要访问 yyparse

extern bool commandRunning;
extern char **environ;  // 环境变量
extern int yyparse(void);
extern FILE *yyin;      // Flex 输入文件
extern void myunputc(int c);  // 用于lex重新解析输入

// 初始化静态成员变量
pid_t Command::_lastBackgroundPid = 0;
int Command::_lastReturnCode = 0;
std::string Command::_lastArgument = "";

Command::Command() {
    // Initialize a new vector of Simple Commands
    _simpleCommands = std::vector<SimpleCommand *>();

    _outFile = NULL;
    _inFile = NULL;
    _errFile = NULL;
    _background = false;
    _appendOut = false;  
    _appendErr = false;  
    _redirectError = false;
}

void Command::insertSimpleCommand( SimpleCommand * simpleCommand ) {
    // add the simple command to the vector
    _simpleCommands.push_back(simpleCommand);
}

void Command::clear() {
    // deallocate all the simple commands in the command vector
    for (auto simpleCommand : _simpleCommands) {
        delete simpleCommand;
    }

    // remove all references to the simple commands we've deallocated
    _simpleCommands.clear();

    // Check if _outFile and _errFile refer to the same object,
    // prevent repeated free causing seg fault
    // ls aaaa | grep jjjj ssss >& out < in
    bool sameOutErr = (_outFile && _errFile && _outFile == _errFile);

    if (_outFile) {
        delete _outFile;
    }
    _outFile = NULL;

    if (_inFile) {
        delete _inFile;
    }
    _inFile = NULL;

    if (_errFile && !sameOutErr) { //If _outFile and _errFile refer to the same object, 
                                    // deleted only once
        delete _errFile;
    }
    _errFile = NULL;

    _background = false;
    _appendOut = false;
    _appendErr = false;
    _redirectError = false; 
}

void Command::print() {
    if(isatty(0)) {
        printf("\n\n");
        printf("              COMMAND TABLE                \n");
        printf("\n");
        printf("  #   Simple Commands\n");
        printf("  --- ----------------------------------------------------------\n");

        int i = 0;
        // iterate over the simple commands and print them nicely
        for ( auto & simpleCommand : _simpleCommands ) {
            printf("  %-3d ", i++ );
            simpleCommand->print();
        }

        printf( "\n\n" );
        printf( "  Output       Input        Error        Background   Append Out   Append Err\n" );
        printf( "  ------------ ------------ ------------ ------------ ------------ ------------\n" );
        printf( "  %-12s %-12s %-12s %-12s %-12s %-12s\n",
                _outFile?_outFile->c_str():"default",
                _inFile?_inFile->c_str():"default",
                _errFile?_errFile->c_str():"default",
                _background?"YES":"NO",
                _appendOut?"YES":"NO",      
                _appendErr?"YES":"NO");     
        printf( "\n\n" );
    }
}

// 执行子shell命令并返回结果
std::string Command::executeSubshell(const std::string &command) {
    int pin[2], pout[2];
    
    // 创建两个管道
    if (pipe(pin) == -1 || pipe(pout) == -1) {
        perror("pipe");
        return "";
    }
    
    // 创建子进程
    pid_t pid = fork();
    
    if (pid == 0) {
        // 子进程
        
        // 设置stdin从pin[0]读取
        dup2(pin[0], 0);
        
        // 设置stdout写入pout[1]
        dup2(pout[1], 1);
        
        // 关闭不需要的管道端
        close(pin[0]);
        close(pin[1]);
        close(pout[0]);
        close(pout[1]);
        
        // 执行当前shell
        char *args[2];
        args[0] = (char *)Shell::_shellPath.c_str();
        args[1] = NULL;
        execvp(args[0], args);
        
        // 如果exec返回，说明出错了
        perror("execvp");
        _exit(1);
    } else if (pid < 0) {
        // 错误处理
        perror("fork");
        close(pin[0]);
        close(pin[1]);
        close(pout[0]);
        close(pout[1]);
        return "";
    }
    
    // 父进程
    // 关闭不需要的管道端
    close(pin[0]);
    close(pout[1]);
    
    // 向子进程写入命令
    std::string cmd = command + "\nexit\n";
    write(pin[1], cmd.c_str(), cmd.length());
    close(pin[1]);  // 关闭写入端，表示输入完成
    
    // 从子进程读取输出
    std::string result;
    char buffer[4096];
    ssize_t bytes_read;
    
    while ((bytes_read = read(pout[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        result += buffer;
    }
    
    close(pout[0]);  // 关闭读取端
    
    // 等待子进程结束
    int status;
    waitpid(pid, &status, 0);
    
    // 处理输出，去除提示符和多余的换行
    size_t pos = 0;
    while ((pos = result.find("myshell>", pos)) != std::string::npos) {
        result.erase(pos, 8);  // 删除"myshell>"
    }
    
    // 替换换行符为空格
    for (size_t i = 0; i < result.length(); i++) {
        if (result[i] == '\n') {
            result[i] = ' ';
        }
    }
    
    // 移除末尾的exit命令输出和空格
    pos = result.find("exit");
    if (pos != std::string::npos) {
        result = result.substr(0, pos);
    }
    
    // 清理末尾空格
    while (!result.empty() && isspace(result.back())) {
        result.pop_back();
    }
    
    return result;
}

// 查找并处理字符串中的子shell命令
std::string Command::processSubshell(const std::string &arg) {
    std::string result;
    size_t i = 0;
    
    while (i < arg.length()) {
        // 查找子shell起始模式 $(
        if (i + 1 < arg.length() && arg[i] == '$' && arg[i+1] == '(') {
            // 找到子shell起始
            size_t start = i;
            size_t endPos = i + 2;
            int depth = 1;
            
            // 查找匹配的右括号
            while (endPos < arg.length() && depth > 0) {
                if (endPos + 1 < arg.length() && arg[endPos] == '$' && arg[endPos+1] == '(') {
                    depth++;
                    endPos++;
                } else if (arg[endPos] == ')') {
                    depth--;
                }
                endPos++;
            }
            
            if (depth == 0) {
                // 找到匹配的右括号
                std::string cmdText = arg.substr(start + 2, endPos - start - 3);
                std::string cmdOutput = executeSubshell(cmdText);
                
                // 将输出添加到结果
                result += cmdOutput;
                i = endPos;
            } else {
                // 未找到匹配的右括号，添加当前字符并继续
                result += arg[i];
                i++;
            }
        } else {
            // 不是子shell，直接添加当前字符
            result += arg[i];
            i++;
        }
    }
    
    return result;
}

// 环境变量扩展实现 - 不使用正则表达式
std::string Command::expandEnvironmentVariables(const std::string &arg) {
    std::stringstream result;
    
    for (size_t i = 0; i < arg.length(); i++) {
        // 寻找 ${var} 模式
        if (i + 1 < arg.length() && arg[i] == '$' && arg[i+1] == '{') {
            // 找到变量名结束位置
            size_t end = arg.find('}', i + 2);
            if (end != std::string::npos) {
                // 提取变量名
                std::string varName = arg.substr(i + 2, end - (i + 2));
                std::string replacement;
                
                // 处理特殊变量
                if (varName == "$") {
                    // Shell进程的PID
                    replacement = std::to_string(getpid());
                } else if (varName == "?") {
                    // 上一个命令的返回码
                    replacement = std::to_string(_lastReturnCode);
                } else if (varName == "!") {
                    // 最后一个后台进程的PID
                    replacement = std::to_string(_lastBackgroundPid);
                } else if (varName == "_") {
                    // 上一个命令的最后一个参数
                    replacement = _lastArgument;
                } else if (varName == "SHELL") {
                    // Shell的可执行路径
                    char path[PATH_MAX];
                    if (realpath(Shell::_shellPath.c_str(), path) != NULL) {
                        replacement = path;
                    } else {
                        replacement = Shell::_shellPath;
                    }
                } else {
                    // 普通环境变量
                    const char *value = getenv(varName.c_str());
                    replacement = value ? value : "";
                }
                
                // 添加替换后的值
                result << replacement;
                
                // 跳过整个变量引用
                i = end;
            } else {
                // 未找到匹配的 }, 保留原字符
                result << arg[i];
            }
        } else {
            // 保留其他字符
            result << arg[i];
        }
    }
    
    return result.str();
}

// 判断是否为内置命令
bool Command::isBuiltInCommand(SimpleCommand *cmd) {
    if (cmd->_arguments.size() == 0) {
        return false;
    }
    
    const char *command = cmd->_arguments[0]->c_str();
    
    return (strcmp(command, "printenv") == 0 || 
            strcmp(command, "setenv") == 0 || 
            strcmp(command, "unsetenv") == 0 || 
            strcmp(command, "cd") == 0 || 
            strcmp(command, "source") == 0);
}

// 判断是否为特定内置命令
bool Command::isPrintEnvCommand(SimpleCommand *cmd) {
    if (cmd->_arguments.size() == 0) {
        return false;
    }
    
    return (strcmp(cmd->_arguments[0]->c_str(), "printenv") == 0);
}

// 打印所有环境变量
void Command::printEnv() {
    char **env = environ;
    while (*env) {
        printf("%s\n", *env);
        env++;
    }
    fflush(stdout);  // 确保输出缓冲区被刷新
}

// 设置环境变量
void Command::setEnv(const char *var, const char *value) {
    if (setenv(var, value, 1) != 0) {
        const char *errMsg = "setenv: Error setting environment variable\n";
        write(2, errMsg, strlen(errMsg));
    }
}

// 移除环境变量
void Command::unsetEnv(const char *var) {
    if (unsetenv(var) != 0) {
        const char *errMsg = "unsetenv: Error unsetting environment variable\n";
        write(2, errMsg, strlen(errMsg));
    }
}

// 更改当前目录
void Command::changeDirectory(const char *dir) {
    const char *target;
    
    if (dir == NULL || strlen(dir) == 0) {
        // 如果未指定目录，切换到HOME目录
        target = getenv("HOME");
        if (target == NULL) {
            const char *errMsg = "cd: HOME not set\n";
            write(2, errMsg, strlen(errMsg));
            return;
        }
    } else {
        target = dir;
    }
    
    if (chdir(target) != 0) {
        std::string errMsg = "cd: can't cd to " + std::string(target) + "\n";
        write(2, errMsg.c_str(), errMsg.length());
    }
}

// 执行脚本文件，逐行执行命令
bool Command::sourceFile(const char *filename) {
    FILE *old_yyin = yyin;  // 保存当前输入
    FILE *file = fopen(filename, "r");
    
    if (!file) {
        std::string errMsg = "source: can't open " + std::string(filename) + "\n";
        write(2, errMsg.c_str(), errMsg.length());
        return false;
    }
    
    // 设置新的输入源
    yyin = file;
    
    // 解析文件中的命令
    bool old_tty = Shell::_isTerminal;
    Shell::_isTerminal = false;  // 在解析脚本文件时禁用终端模式
    
    // 临时状态标记
    bool originalCommandRunning = commandRunning;
    commandRunning = false;
    
    // 执行命令
    yyparse();
    
    // 恢复设置
    Shell::_isTerminal = old_tty;
    yyin = old_yyin;
    fclose(file);
    
    // 恢复原始状态标记
    commandRunning = originalCommandRunning;
    
    return true;
}

void Command::execute() {
    // Don't do anything if there are no simple commands
    if (_simpleCommands.size() == 0 || _redirectError) {
        clear();
        Shell::prompt();
        return;
    }

    // 在执行命令前保存最后一条命令的最后一个参数（如果有）
    if (_simpleCommands.size() > 0) {
        SimpleCommand *lastCommand = _simpleCommands.back();
        if (lastCommand->_arguments.size() > 0) {
            _lastArgument = *(lastCommand->_arguments.back());
        }
    }

    // 设置命令正在运行标志
    commandRunning = true;

    // Print contents of Command data structure
    print();

    // Save standard input, output, and error for restoration later
    int tmpin = dup(0);
    int tmpout = dup(1);
    int tmperr = dup(2);

    // Set up redirection for input
    int fdin;
    if (_inFile) {
        // Open input file
        fdin = open(_inFile->c_str(), O_RDONLY);
        if (fdin < 0) {
            perror("open infile");
            clear();
            Shell::prompt();
            return;
        }
    } else {
        // Use default input(stdin)
        fdin = dup(tmpin);
    }

    int fdout;
    pid_t pid;
    std::vector<pid_t> childPids; // Save all childPids for later waiting
    
    // For each simple command
    for (size_t i = 0; i < _simpleCommands.size(); i++) {
        // 先处理子shell命令，再进行环境变量扩展
        SimpleCommand *simpleCommand = _simpleCommands[i];
        for (size_t j = 0; j < simpleCommand->_arguments.size(); j++) {
            // 处理子shell命令 $(command)
            std::string processed = processSubshell(*(simpleCommand->_arguments[j]));
            
            // 对处理后的参数进行环境变量扩展
            std::string expanded = expandEnvironmentVariables(processed);
            
            // 更新参数
            delete simpleCommand->_arguments[j];
            simpleCommand->_arguments[j] = new std::string(expanded);
        }
        
        // Redirect input from previous command or input file
        dup2(fdin, 0);
        close(fdin);

        // If it's the last command, Setup output redirection
        if (i == _simpleCommands.size() - 1) {
            if (_outFile) {
                int flags = O_CREAT | O_WRONLY;
                if (_appendOut) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                fdout = open(_outFile->c_str(), flags, 0664);
                if (fdout < 0) {
                    perror("open outfile");
                    clear();
                    Shell::prompt();
                    return;
                }
            } else {
                // Use default output
                fdout = dup(tmpout);
            }

            // Setup error redirection
            if (_errFile) {
                int flags = O_CREAT | O_WRONLY;
                if (_appendErr) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                int fderr = open(_errFile->c_str(), flags, 0664);
                if (fderr < 0) {
                    perror("open errfile");
                    close(fdout);
                    clear();
                    Shell::prompt();
                    return;
                }
                dup2(fderr, 2);
                close(fderr);
            } else {
                // Use default error
                dup2(tmperr, 2);
            }
        } else {
            // Not the last command - create a pipe
            int fdpipe[2];
            if (pipe(fdpipe) == -1) {
                perror("pipe");
                clear();
                Shell::prompt();
                return;
            }
            fdout = fdpipe[1];  //write end
            fdin = fdpipe[0];  //read end
        }

        // Redirect output to fdout
        dup2(fdout, 1);
        close(fdout);

        // 检查命令类型
        bool isBuiltin = isBuiltInCommand(simpleCommand);
        
        // 命令处理
        if (isBuiltin) {
            const char *cmd = simpleCommand->_arguments[0]->c_str();
            
            if (strcmp(cmd, "printenv") == 0) {
                // printenv在父进程中执行，但需要支持管道
                printEnv();
                _lastReturnCode = 0;
            }
            else if (strcmp(cmd, "setenv") == 0) {
                if (simpleCommand->_arguments.size() < 3) {
                    const char *errMsg = "setenv: Too few arguments\n";
                    write(2, errMsg, strlen(errMsg));
                } else {
                    setEnv(simpleCommand->_arguments[1]->c_str(), simpleCommand->_arguments[2]->c_str());
                }
                _lastReturnCode = 0;
            }
            else if (strcmp(cmd, "unsetenv") == 0) {
                if (simpleCommand->_arguments.size() < 2) {
                    const char *errMsg = "unsetenv: Too few arguments\n";
                    write(2, errMsg, strlen(errMsg));
                } else {
                    unsetEnv(simpleCommand->_arguments[1]->c_str());
                }
                _lastReturnCode = 0;
            }
            else if (strcmp(cmd, "cd") == 0) {
                const char *dir = (simpleCommand->_arguments.size() > 1) ? 
                                 simpleCommand->_arguments[1]->c_str() : NULL;
                changeDirectory(dir);
                _lastReturnCode = 0;
            }
            else if (strcmp(cmd, "source") == 0) {
                if (simpleCommand->_arguments.size() < 2) {
                    const char *errMsg = "source: Too few arguments\n";
                    write(2, errMsg, strlen(errMsg));
                } else {
                    sourceFile(simpleCommand->_arguments[1]->c_str());
                }
                _lastReturnCode = 0;
            }
        }
        else {
            // 普通命令，在子进程中执行
            pid = fork();
            if (pid == 0) {
                // 执行外部命令
                int numArgs = simpleCommand->_arguments.size();
                char **args = new char*[numArgs + 1];
                
                for (int j = 0; j < numArgs; j++) {
                    args[j] = (char *)simpleCommand->_arguments[j]->c_str();
                }
                args[numArgs] = NULL;
                
                execvp(args[0], args);
                
                perror("execvp");
                delete[] args;
                _exit(1);
            } else if (pid < 0) {
                perror("fork");
                _exit(1);
            }
            childPids.push_back(pid);
        }
    }

    // Restore stdin, stdout, and stderr
    dup2(tmpin, 0);
    dup2(tmpout, 1);
    dup2(tmperr, 2);
    close(tmpin);
    close(tmpout);
    close(tmperr);

    // Wait for commands to finish if not background
    if (!_background) {
        for (size_t i = 0; i < childPids.size(); i++) {
            int status;
            waitpid(childPids[i], &status, 0);
            
            if (i == childPids.size() - 1) {
                if (WIFEXITED(status)) {
                    _lastReturnCode = WEXITSTATUS(status);
                } else {
                    _lastReturnCode = 1;
                }
            }
        }
    } else if (!childPids.empty()) {
        _lastBackgroundPid = childPids.back();
        printf("[%d] %d\n", 1, _lastBackgroundPid);
    }

    // 命令执行完毕，重置标志
    commandRunning = false;

    // 清理并始终显示提示符，无论是前台还是后台命令
    clear();
    Shell::prompt();
}

SimpleCommand * Command::_currentSimpleCommand;
