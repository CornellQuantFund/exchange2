#pragma once

#include "Usings.h"

// Every quote has a price and size(quantity)
struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;