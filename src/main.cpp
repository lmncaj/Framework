#if defined(DEBUG)||defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "App.h"

int wmain(int argc, wchar_t** argv, wchar_t** envp)
{

	App app(960, 540);
	app.Run();
	return 0;
}


