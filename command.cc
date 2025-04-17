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
#include "y.tab.hh"     // yyparse

extern bool commandRunning;
extern char **environ;  // global var: Environment variable
extern int yyparse(void);
extern FILE *yyin;      // Flex
extern void myunputc(int c);  // for lex

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

// environmant expansion 3.1
std::string Command::expandEnvironmentVariables(const std::string &arg) {
    std::stringstream result;
    
    for (size_t i = 0; i < arg.length(); i++) {
        // search for pattern: ${var} 
        if (i + 1 < arg.length() && arg[i] == '$' && arg[i+1] == '{') {
            // search for the ending of "var"
            size_t end = arg.find('}', i + 2);
            if (end != std::string::npos) {
                // extract "var" -> varName
                std::string varName = arg.substr(i + 2, end - (i + 2));
                std::string replacement;
                
                if (varName == "$") {
                    // PID of the shell process
                    replacement = std::to_string(getpid());
                } else if (varName == "?") {
                    // return code of the last executed simple command
                    replacement = std::to_string(_lastReturnCode);
                } else if (varName == "!") {
                    // PID of the last process run in the background
                    replacement = std::to_string(_lastBackgroundPid);
                } else if (varName == "_") {
                    // last argument in the fully expanded previous command
                    replacement = _lastArgument;
                } else if (varName == "SHELL") {
                    // path of your shell executable
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

// check if it's a builtin command
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

// check if it's the printenv command
bool Command::isPrintEnvCommand(SimpleCommand *cmd) {
    if (cmd->_arguments.size() == 0) {
        return false;
    }
    return (strcmp(cmd->_arguments[0]->c_str(), "printenv") == 0);
}

// execute printenv, printing all environment variables
void Command::printEnv() {
    char **env = environ;
    while (*env) {
        printf("%s\n", *env);
        env++;
    }
    fflush(stdout);  
}

// setenv
void Command::setEnv(const char *var, const char *value) {
    if (setenv(var, value, 1) != 0) { // 1： Overwrite if already existing
        const char *errMsg = "setenv: Error setting environment variable\n";
        write(2, errMsg, strlen(errMsg));
    }
}

// unsetenv
void Command::unsetEnv(const char *var) {
    if (unsetenv(var) != 0) {
        const char *errMsg = "unsetenv: Error unsetting environment variable\n";
        write(2, errMsg, strlen(errMsg));
    }
}

// cd
void Command::changeDirectory(const char *dir) {
    const char *target;
    
    if (dir == NULL || strlen(dir) == 0) {
        // If no directory is specified, default to the home
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

// source: incorrect yet
bool Command::sourceFile(const char *filename) {
    FILE *old_yyin = yyin;  //save old input 
    FILE *file = fopen(filename, "r");
    
    if (!file) {
        std::string errMsg = "source: can't open " + std::string(filename) + "\n";
        write(2, errMsg.c_str(), errMsg.length());
        return false;
    }
    
    // set new input
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
        // environment var expansion
        SimpleCommand *simpleCommand = _simpleCommands[i];
        for (size_t j = 0; j < simpleCommand->_arguments.size(); j++) {
            // expansion
            std::string expanded = expandEnvironmentVariables(*(simpleCommand->_arguments[j]));
            
            // update arguments
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

        // Determine if the 1st parameter is "printenv, setenv, unsetenv, cd, source"
        bool isBuiltin = isBuiltInCommand(simpleCommand);
        
        // handle commands
        if (isBuiltin) {
            const char *cmd = simpleCommand->_arguments[0]->c_str();
            
            if (strcmp(cmd, "printenv") == 0) {
                // printenv: Execute in the parent process
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
            // common command, execute in child
            pid = fork();
            if (pid == 0) {
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

    commandRunning = false;

    clear();
    Shell::prompt();
}

SimpleCommand * Command::_currentSimpleCommand;
