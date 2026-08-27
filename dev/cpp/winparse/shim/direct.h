#pragma once
#include <sys/stat.h>
inline int _mkdir(const char* p) { return ::mkdir(p, 0755); }
inline char* _getcwd(char* b, int n) { return ::getcwd(b, (unsigned long)n); }
#include <unistd.h>
