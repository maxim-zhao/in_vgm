#include "PlatformDependent.h"
#include <cstdio> // for FILE stuff

int PlatformDependent::getFileSize(const std::string& filename)
{
	int size = -1;
	FILE* f = fopen(filename.c_str(), "rb");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fclose(f);
	}
	return size;
}