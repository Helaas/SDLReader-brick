#pragma once

#ifdef NDEBUG
#define DEBUG_LOG(x) do {} while(0)
#define DEBUG_ERR(x) do {} while(0)
#else
#include <iostream>
#define DEBUG_LOG(x) do { std::cout << x << std::endl; } while(0)
#define DEBUG_ERR(x) do { std::cerr << x << std::endl; } while(0)
#endif
