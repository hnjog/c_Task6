#include "ItemManager.h"
#include "DataManager.h"

void ItemManager::Init()
{
	ItemDatas.clear();
	DataManager& DM = DataManager::Instance();
	if (DM.Initialize() == false)
		return;

	ItemDatas = DM.TakeItems();
}

void ItemManager::PrintAllItems()
{
	for (auto& item : ItemDatas)
	{
		std::cout << "==========================" << '\n';
		std::cout << "아이템 이름 : " << item.name << '\n';
		std::cout << "아이템 효과 : " << item.effect << '\n';
		std::cout << "아이템 수치 : " << item.value << '\n';
		std::cout << "아이템 인덱스 : " << item.idx << '\n';
	}
}
