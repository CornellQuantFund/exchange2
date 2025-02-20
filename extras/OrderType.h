#pragma once

enum class OrderType {
	// Currently supporting goodtilcancel and market
	GoodTillCancel,
	FillAndKill,
	FillOrKill,
	GoodForDay,
	Market,
};