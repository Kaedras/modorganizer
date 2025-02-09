#pragma once

#include <iostream>

#define STUB() std::cout << __FILE_NAME__ << ": " << __PRETTY_FUNCTION__ << ": STUB!\n"
#define STUB_PARAM(param)                                                              \
  std::cout << __FILE_NAME__ << ": " << __PRETTY_FUNCTION__                            \
            << ": STUB! parameter: " << param << "\n"
