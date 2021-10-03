#pragma once
#include "tweaks.h"

#ifndef PERIMETER_HEADLESS
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
//#define _WIN32_WINNT  0x0400
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <dplay8.h>
#endif

// Standart includes
#include <climits>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <direct.h>

#if (!defined(_FINAL_VERSION_) || defined(_DEBUG)) && !defined(NASSERT)
#include <iostream>
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1900)
// non-standard header : https://developercommunity.visualstudio.com/t/msvc-142328019-is-missing-include-typeinfoh/734566
#include <typeinfo.h>
#else
#include <typeinfo>
#endif
#include <float.h>
#include <mmsystem.h>
#include <commctrl.h>

// STL
#include <random>
#include <vector>
#include <list>
#include <unordered_map>

#include <map>
#include <string>
#include <deque>
#include <algorithm>

// XTool
#include "xutil.h"
#include "Serialization.h"
#include "xmath.h"
#include "Timers.h"

#include "Umath.h"
#include "IRenderDevice.h"
#include "IVisGeneric.h"
#include "VisGenericDefine.h"

#include "SystemUtil.h"
#include "DebugUtil.h"

#include "ConnectionDP.h"
#include "EventBufferDP.h"
#include "CommonEvents.h"

//TODO maybe we should remove STARFORCE stuff?
//#define _STARFORCE_

#ifdef _STARFORCE_
#define STARFORCE_API extern "C" __declspec(dllexport)
#define STARFORCE_API_NEW extern "C" __declspec(dllexport)
#else
#define STARFORCE_API 
#define STARFORCE_API_NEW 
#endif

#ifdef small
#undef small
#endif
