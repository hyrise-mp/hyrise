#pragma once

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#define DEV_ONLY
// The DEV_ONLY keyword does nothing. It is used to mark functions that should not be called from productive code (e.g.,
// because they are slow).
// Functions marked DEV_ONLY must only be called by other functions marked DEV_ONLY. Unfortunately, there is no easy way
// to enforce this. See:
// http://nwcpp.org/talks/2007/redcode_-_updated.pdf
// https://herbsutter.com/2007/05/06/thoughts-on-scotts-red-code-green-code-talk/

namespace opossum {
using std::to_string;
}