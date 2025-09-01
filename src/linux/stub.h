#ifndef STUB_H
#define STUB_H

#include <iostream>

#define STUB()                                                                         \
  _Pragma("GCC warning \"STUB!\"") std::cout << __FILE_NAME__ << ": "                  \
                                             << __PRETTY_FUNCTION__ << ": STUB!\n"
#define STUB_PARAM(param)                                                              \
  _Pragma("GCC warning \"STUB!\"") std::cout << __FILE_NAME__ << ": "                  \
                                             << __PRETTY_FUNCTION__                    \
                                             << ": STUB! parameter: " << param << "\n"

#endif  // STUB_H
