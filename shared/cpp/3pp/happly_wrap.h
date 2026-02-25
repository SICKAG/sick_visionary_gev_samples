#pragma once


// Silencing warnings coming from the happly-helper-project without need to touch its code directly

#ifdef WIN32
#pragma warning(push)
#pragma warning(disable : 4456)
#pragma warning(disable : 4244)
#endif

#include "happly.h"

#ifdef WIN32
#pragma warning(pop)
#endif

