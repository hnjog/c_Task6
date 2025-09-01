#include <iostream>
#include "DataManager.h"
#include "ItemManager.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
#ifdef _WIN32
    // 콘솔 입출력 코드페이지를 UTF-8로
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    ItemManager itemMgr;
    itemMgr.Init();
    itemMgr.PrintAllItems();

}