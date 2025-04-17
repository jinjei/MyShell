#ifndef command_hh
#define command_hh

#include "simpleCommand.hh"
#include <vector>

// Command Data Structure

struct Command {
  std::vector<SimpleCommand *> _simpleCommands;
  std::string *_outFile;
  std::string *_inFile;
  std::string *_errFile;
  bool _background;
  bool _appendOut;
  bool _appendErr;
  bool _redirectError;

  Command();
  void insertSimpleCommand( SimpleCommand * simpleCommand );
  void clear();
  void print();
  void execute();

  // 添加内置命令处理函数
  bool isBuiltInCommand(SimpleCommand *cmd);
  bool isPrintEnvCommand(SimpleCommand *cmd);
  bool executeBuiltInCommand(SimpleCommand *cmd, bool pipeline);
  
  // 各个内置命令的实现
  void printEnv();
  void setEnv(const char *var, const char *value);
  void unsetEnv(const char *var);
  void changeDirectory(const char *dir);
  bool sourceFile(const char *file);

  // 环境变量扩展功能
  static std::string expandEnvironmentVariables(const std::string &arg);
  
  // 特殊环境变量记录
  static pid_t _lastBackgroundPid;
  static int _lastReturnCode;
  static std::string _lastArgument;

  static SimpleCommand *_currentSimpleCommand;
};

#endif