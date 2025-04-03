/*
 * shell.l: lexical analyzer for shell
 */

%{
#include <cstring>
#include "y.tab.hh"

static void yyunput(int c, char *buf_ptr);

void myunputc(int c) {
  unput(c);
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

["][^\n\"]*["] {
  /* 处理引号字符串 - 去除首尾引号 */
  yylval.cpp_string = new std::string(yytext);
  *yylval.cpp_string = yylval.cpp_string->substr(1, yylval.cpp_string->length()-2);
  return WORD;
}

[^ \t\n\>\<\|&]*\\[^ \t\n]* {
  /* 处理转义字符 */
  char *str = strdup(yytext);
  char *newstr = (char*) malloc(strlen(str) + 1);
  
  int i = 0, j = 0;
  while (str[i]) {
    if (str[i] == '\\') {
      if (str[i+1] == '\\') {
        // 两个连续反斜杠 - 只输出一个反斜杠
        newstr[j++] = '\\';
        i += 3; // 跳过两个反斜杠
      } else if (str[i+1]) {
        // 单个反斜杠后跟其他字符 - 保留被转义的字符
        newstr[j++] = str[i+1];
        i += 2; // 跳过反斜杠和被转义的字符
      } else {
        // 单个反斜杠在字符串末尾
        newstr[j++] = '\\';
        i++;
      }
    } else {
      // 普通字符
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
  /* 普通单词 */
  yylval.cpp_string = new std::string(yytext);
  return WORD;
}

%%
