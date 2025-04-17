#ifndef shell_hh
#define shell_hh

#include "command.hh"


struct Shell {

  static void prompt();
  static bool isTerminal();
  static bool promptNeeded;  //标记是否需要显示提示符
  static Command _currentCommand;
  static bool _isTerminal;
  static std::string _shellPath; // 存储Shell可执行文件路径
};

#endif
