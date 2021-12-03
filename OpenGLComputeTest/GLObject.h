#pragma once
#include <glad4.3/glad4.3.h>

class GLObject
{
public:
	virtual void bind()		= 0;
	virtual void unbind()	= 0;
protected:
	unsigned int m_handle;
};