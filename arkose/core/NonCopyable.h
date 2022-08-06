#pragma once

#define NON_COPYABLE(X)              \
	X(const X&) = delete;            \
	X& operator=(const X&) = delete;
