#pragma once

#include<string>

enum ItemType
{
	IT_NONE,
	IT_CONSUME,
};

struct ItemBase
{
	int idx;
	ItemType type;
	std::string name;
	std::string effect;
	int value;
};