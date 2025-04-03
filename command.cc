#include <cstdio>
#include <cstdlib>
#include <unistd.h>     //对于dup, dup2, pipe等系统调用
#include <wait.h>       //waitpid系统调用
#include <sys/types.h>  //pid_t
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>

#include "command.hh"
#include "shell.hh"

extern bool commandRunning;

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


void Command::execute() {
    // Don't do anything if there are no simple commands
    if ( _simpleCommands.size() == 0 || _redirectError) {
        clear();
        Shell::prompt();
        return;
    }

    // 设置命令正在运行标志
    commandRunning = true;

    // Print contents of Command data structure
    print();

    // Add execution here
    // For every simple command fork a new process
    // Setup i/o redirection
    // and call exec

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
        // Redirect input from previous command or input file
        // dup2(int oldfd, int newfd);  make newfd is a copy of oldfd
        dup2(fdin, 0);
        close(fdin);

        // If it's the last command, Setup output redirection
        if (i == _simpleCommands.size() - 1) {
            if (_outFile) {
                int flags = O_CREAT | O_WRONLY; // '|': bitwise OR
                                                // flags now: create and write
                if (_appendOut) {
                    flags |= O_APPEND;  // append
                } else {
                    flags |= O_TRUNC;   // cover (truncate file to length 0)
                }
                // If create a new file, the permission will be 0664(base 8)
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

        //need set up all the redirect info before execvp
        // Create child process
        pid = fork();
        if (pid == 0) { // execute commands in Child process
            
            // Create argument array for execvp
            SimpleCommand *simpleCommand = _simpleCommands[i];
            int numArgs = simpleCommand->_arguments.size();
            char **args = new char*[numArgs + 1]; // +1 for NULL terminator
            
            for (int j = 0; j < numArgs; j++) {
                args[j] = (char *)simpleCommand->_arguments[j]->c_str();
            }
            args[numArgs] = NULL;
            
            // Execute command, 1st arg: the name of the executable to load
            //2nd arg: an array of the arguments, NULL at the end
            execvp(args[0], args);
            
            // If execvp returns, there was an error
            perror("execvp");
            delete[] args;
            _exit(1);
        } else if (pid < 0) {
            // Fork failed
            perror("fork");
            clear();
            Shell::prompt();
            return;
        }
        
        // Parent continues - store child pid
        childPids.push_back(pid);
    }

    // Restore stdin, stdout, and stderr
    dup2(tmpin, 0);  //make 0 a copy of tmpin(previously restored stdin) 
    dup2(tmpout, 1); 
    dup2(tmperr, 2);
    close(tmpin);
    close(tmpout);
    close(tmperr);

    // Wait for last command to finish if not background
    if (!_background) {
        // Wait for all child processes
        for (pid_t childPid : childPids) {
            // waitpid(pid_t pid, int *status, int options);
            waitpid(childPid, NULL, 0);
        }
    } else {
        // print the PID of the background process
        printf("[%d] %d\n", 1, childPids.back());
    }

    // 命令执行完毕，重置标志
    commandRunning = false;

    // 清理并始终显示提示符，无论是前台还是后台命令
    clear();
    Shell::prompt();
}

SimpleCommand * Command::_currentSimpleCommand;
