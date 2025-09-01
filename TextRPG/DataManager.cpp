#include "DataManager.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdlib> // strtol

#ifdef _WIN32
#include <windows.h>
#include <codecvt>
#else
#include <unistd.h>
#include <limits.h>
#endif

#include <iostream>

// ---------- Singleton ----------
DataManager& DataManager::Instance()
{
	static DataManager inst;
	return inst;
}

// ---------- Path / IO ----------
#ifdef _WIN32
std::string DataManager::ToUtf8(const std::wstring& w) const
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
	return conv.to_bytes(w);
}
std::wstring DataManager::ToWide(const std::string& u8) const
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
	return conv.from_bytes(u8);
}
#endif

std::string DataManager::ResolveFromResourcesOutput(const std::string& relative) const
{
#ifdef _WIN32
	std::wstring full = ToWide("..\\Resources\\output\\");
	full += ToWide(relative);
	return ToUtf8(full);
#else
	return "../Resources/output/" + relative;
#endif
}

// ---- 헬퍼: 바이트 프리픽스 검사
static bool has_prefix(const std::vector<unsigned char>& buf,
	std::initializer_list<unsigned char> p) {
	if (buf.size() < p.size()) return false;
	size_t i = 0;
	for (auto v : p) { if (buf[i++] != v) return false; }
	return true;
}

std::string DataManager::ReadFileToString(const std::string& pathUtf8) const
{
#ifdef _WIN32
	std::ifstream ifs(ToWide(pathUtf8), std::ios::binary);
#else
	std::ifstream ifs(pathUtf8.c_str(), std::ios::binary);
#endif
	if (!ifs) throw std::runtime_error("failed to open: " + pathUtf8);

	// 전체 읽기
	std::vector<unsigned char> buf;
	ifs.seekg(0, std::ios::end);
	std::streamoff sz = ifs.tellg();
	ifs.seekg(0, std::ios::beg);
	if (sz > 0)
	{
		buf.resize(static_cast<size_t>(sz));
		ifs.read(reinterpret_cast<char*>(buf.data()), sz);
	}
	if (buf.empty())
		return std::string();

	// --- 인코딩 처리 ---
	// 1) UTF-8 BOM → 제거
	if (has_prefix(buf, { 0xEF, 0xBB, 0xBF })) {
		return std::string(reinterpret_cast<const char*>(buf.data() + 3),
			reinterpret_cast<const char*>(buf.data() + buf.size()));
	}

	// 2) UTF-16 LE → UTF-8 변환
	if (has_prefix(buf, { 0xFF, 0xFE })) {
		const uint16_t* w = reinterpret_cast<const uint16_t*>(buf.data() + 2);
		size_t wc = (buf.size() - 2) / 2;
		std::wstring ws; ws.resize(wc);
		for (size_t i = 0; i < wc; ++i) ws[i] = static_cast<wchar_t>(w[i]);
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
		return conv.to_bytes(ws);
	}

	// 3) UTF-16 BE → UTF-8 변환
	if (has_prefix(buf, { 0xFE, 0xFF })) {
		const unsigned char* p = buf.data() + 2;
		size_t wc = (buf.size() - 2) / 2;
		std::wstring ws; ws.resize(wc);
		for (size_t i = 0; i < wc; ++i) {
			uint16_t u = (static_cast<uint16_t>(p[2 * i]) << 8) | static_cast<uint16_t>(p[2 * i + 1]);
			ws[i] = static_cast<wchar_t>(u);
		}
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
		return conv.to_bytes(ws);
	}

	// 4) 그 외는 UTF-8/ASCII로 간주
	return std::string(reinterpret_cast<const char*>(buf.data()),
		reinterpret_cast<const char*>(buf.data() + buf.size()));
}
JsonValue DataManager::ParseJsonFile(const std::string& pathUtf8) const
{
	std::string s = ReadFileToString(pathUtf8);
	JsonParser p(s);
	return p.parse();
}

// ---------- Initialize (부팅 시 1회) ----------
bool DataManager::Initialize()
{
	if (bInitialized)
		return bInitialized;

	// items.json (최상위 배열: {"Idx","Name","Effect","Type","Value"})
	try
	{
		std::string path = ResolveFromResourcesOutput("Item.json");
		JsonValue root = ParseJsonFile(path);
		LoadItemsJson(root);
	}
	catch (...)
	{
		std::cout << "Item.json do not exist!" << '\n';
	}

	try
	{
		std::string path = ResolveFromResourcesOutput("Shop.json");
		JsonValue root = ParseJsonFile(path);
		//LoadShopJson(root);
	}
	catch (...)
	{
		std::cout << "Shop.json do not exist!" << '\n';
	}

	bInitialized = true;

	return bInitialized;
}

// ---------- JSON → Items ----------
void DataManager::LoadItemsJson(const JsonValue& root)
{
	ItemDataVector.clear();

	// 우리가 쓰는 스키마: 최상위가 배열
	if (root.type != JsonValue::Type::Array)
		return;

	ItemDataVector.reserve(root.arr.size());
	for (const auto& obj : root.arr) {
		if (obj.type != JsonValue::Type::Object)
			continue;

		ItemBase it;

		const JsonValue* pIdx = obj.get("Idx");
		if (pIdx)
		{
			if (pIdx->type == JsonValue::Type::String)
				it.idx = static_cast<int>(std::strtol(pIdx->str.c_str(), nullptr, 10));
			else if (pIdx->type == JsonValue::Type::Number)
				it.idx = static_cast<int>(pIdx->number);
		}

		const JsonValue* pName = obj.get("Name");
		if (pName && pName->type == JsonValue::Type::String)
			it.name = (pName->str);

		const JsonValue* pEffect = obj.get("Effect");
		if (pEffect && pEffect->type == JsonValue::Type::String)
			it.effect = (pEffect->str);

		const JsonValue* pType = obj.get("Type");
		if (pType && pType->type == JsonValue::Type::String)
		{
			it.type = ParseItemType(pType->str);
		}

		const JsonValue* pValue = obj.get("Value");
		if (pValue)
		{
			if (pValue->type == JsonValue::Type::String)
				it.value = static_cast<int>(std::strtol(pValue->str.c_str(), nullptr, 10));
			else if (pValue->type == JsonValue::Type::Number)
				it.value = static_cast<int>(pValue->number);
		}

		ItemDataVector.push_back(it);
	}
}

ItemType DataManager::ParseItemType(const std::string& sRaw)
{
	std::string s;
	s.reserve(sRaw.size());
	for (char c : sRaw)
		s.push_back((char)std::tolower((unsigned char)c));
	if (s == "consume")
		return IT_CONSUME;
	return IT_NONE;
}

// ---------- Move-out ----------
std::vector<ItemBase> DataManager::TakeItems()
{
	return std::move(ItemDataVector);
}