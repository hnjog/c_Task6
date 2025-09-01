#pragma once

#include <vector>
#include <string>

#include"ItemBase.h"

class ItemManager
{
public:
    void Init();

    void PrintAllItems();

private:
    std::vector<ItemBase> ItemDatas;
};

