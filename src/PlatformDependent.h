#ifndef PlatformDependent_h__
#define PlatformDependent_h__

#include <string>

class PlatformDependent
{
public:
	static int getFileSize(const std::string& filename);
};

#endif // PlatformDependent_h__