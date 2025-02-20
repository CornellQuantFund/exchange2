#pragma once

#include <limits>

#include "Usings.h"

// Used for invalid prices
struct Constants
{
    static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};