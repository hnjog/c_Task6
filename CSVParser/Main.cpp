// mycsv2json.cpp : C++17, 단일 파일, 외부 라이브러리 없이 빌드 가능
// 빌드 예: g++ -std=c++17 -O2 -o mycsv2json mycsv2json.cpp
//        cl /std:c++17 /O2 mycsv2json.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
namespace fs = std::filesystem;

// -------------------- 유틸: 트림 --------------------
static inline string ltrim(string s) { s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char c) {return !isspace(c); })); return s; }
static inline string rtrim(string s) { s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char c) {return !isspace(c); }).base(), s.end()); return s; }
static inline string trim(string s) { return rtrim(ltrim(std::move(s))); }

// UTF-8 BOM 제거
static inline void strip_utf8_bom(std::string& s) {
	if (s.size() >= 3 &&
		(unsigned char)s[0] == 0xEF &&
		(unsigned char)s[1] == 0xBB &&
		(unsigned char)s[2] == 0xBF) {
		s.erase(0, 3);
	}
}

// 간단한 UTF-8 유효성 검사 (BOM 유무와 무관)
static bool looks_like_utf8(const std::string& s) {
	const unsigned char* p = (const unsigned char*)s.data();
	size_t i = 0, n = s.size();
	while (i < n) {
		unsigned char c = p[i];
		if (c < 0x80) { i++; continue; }               // ASCII
		else if ((c >> 5) == 0x6) {                         // 110xxxxx
			if (i + 1 >= n) return false;
			if ((p[i + 1] >> 6) != 0x2) return false;
			i += 2;
		}
		else if ((c >> 4) == 0xE) {                        // 1110xxxx
			if (i + 2 >= n) return false;
			if ((p[i + 1] >> 6) != 0x2 || (p[i + 2] >> 6) != 0x2) return false;
			i += 3;
		}
		else if ((c >> 3) == 0x1E) {                       // 11110xxx
			if (i + 3 >= n) return false;
			if ((p[i + 1] >> 6) != 0x2 || (p[i + 2] >> 6) != 0x2 || (p[i + 3] >> 6) != 0x2) return false;
			i += 4;
		}
		else {
			return false;
		}
	}
	return true;
}

#ifdef _WIN32
// 임의 코드페이지 → UTF-8
static std::string cp_to_utf8(const std::string& s, unsigned codepage) {
	if (s.empty()) return {};
	int wlen = MultiByteToWideChar(codepage, 0, s.data(), (int)s.size(), nullptr, 0);
	if (wlen <= 0) return {};
	std::wstring w(wlen, L'\0');
	MultiByteToWideChar(codepage, 0, s.data(), (int)s.size(), &w[0], wlen);
	int u8len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string u8(u8len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &u8[0], u8len, nullptr, nullptr);
	return u8;
}
#endif

// -------------------- A1 표기 -> (row,col) 0기반 --------------------
struct CellPos { size_t row; size_t col; };
bool a1ToRowCol(const string& a1, CellPos& out) {
	// 예: "G8" -> col=6 (0부터), row=7 (0부터)
	// [A-Z]+ [0-9]+
	if (a1.empty()) return false;
	size_t i = 0;
	long long col = 0;
	while (i < a1.size() && isalpha((unsigned char)a1[i])) {
		col = col * 26 + (toupper((unsigned char)a1[i]) - 'A' + 1);
		++i;
	}
	if (i == 0 || i >= a1.size()) return false;
	long long row = 0;
	while (i < a1.size() && isdigit((unsigned char)a1[i])) {
		row = row * 10 + (a1[i] - '0');
		++i;
	}
	if (i != a1.size() || row <= 0 || col <= 0) return false;
	out.row = (size_t)(row - 1);
	out.col = (size_t)(col - 1);
	return true;
}

// -------------------- CSV 파서 (따옴표/콤마/개행 처리) --------------------
vector<string> parseCsvLine(const string& line) {
	vector<string> out;
	string cur;
	bool inQuotes = false;
	for (size_t i = 0; i < line.size(); ++i) {
		char c = line[i];
		if (inQuotes) {
			if (c == '"') {
				if (i + 1 < line.size() && line[i + 1] == '"') { // "" -> "
					cur.push_back('"'); ++i;
				}
				else {
					inQuotes = false;
				}
			}
			else {
				cur.push_back(c);
			}
		}
		else {
			if (c == '"') { inQuotes = true; }
			else if (c == ',') { out.push_back(cur); cur.clear(); }
			else { cur.push_back(c); }
		}
	}
	out.push_back(cur);
	return out;
}

// -------------------- 간단 JSON 직렬화 (string escape 포함) --------------------
string jsonEscape(const string& s) {
	ostringstream oss;
	for (unsigned char c : s) {
		switch (c) {
		case '\"': oss << "\\\""; break;
		case '\\': oss << "\\\\"; break;
		case '\b': oss << "\\b"; break;
		case '\f': oss << "\\f"; break;
		case '\n': oss << "\\n"; break;
		case '\r': oss << "\\r"; break;
		case '\t': oss << "\\t"; break;
		default:
			if (c < 0x20) {
				oss << "\\u" << hex << setw(4) << setfill('0') << (int)c;
			}
			else {
				oss << c;
			}
		}
	}
	return oss.str();
}

string jsonString(const string& s) { return string("\"") + jsonEscape(s) + "\""; }

string jsonObject(const vector<pair<string, string>>& kvs) {
	// kvs: key is already string literal (no quotes), value should be valid JSON literal
	// value는 이미 jsonString() 등으로 적절히 만들어 전달
	string out = "{";
	for (size_t i = 0; i < kvs.size(); ++i) {
		if (i) out += ',';
		out += jsonString(kvs[i].first);
		out += ':';
		out += kvs[i].second;
	}
	out += "}";
	return out;
}

string jsonArray(const vector<string>& elems) {
	string out = "[";
	for (size_t i = 0; i < elems.size(); ++i) {
		if (i) out += ',';
		out += elems[i];
	}
	out += "]";
	return out;
}

// -------------------- 설정 구조 --------------------
struct SheetConf {
	string startCell = "A1";
	vector<string> columns; // 가로 방향 필드명
};

struct Config {
	// 시트명(CSV 파일명(확장자 제외)) -> 설정
	unordered_map<string, SheetConf> sheets;
	bool stopOnEmptyFirstColumn = true;

	// "auto" | "utf8" | "cp949"
	std::string inputEncoding = "auto";
	bool outputUtf8Bom = true;
};

// config.json 간단 파서(아주 제한적; 따옴표/콤마/콜론/중괄호만, 공백허용)
bool loadConfigJson(const fs::path& path, Config& cfg) {
	if (!fs::exists(path)) return false;
	ifstream in(path);
	if (!in) return false;
	// 매우 단순 파서: 실제 프로덕션에선 JSON 라이브러리 권장
	// 형식은 위 예시 형태만 처리한다 가정.
	string all((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
	// 토크나이즈 대충…
	auto findStr = [&](const string& key)->vector<size_t> {
		vector<size_t> pos;
		size_t start = 0;
		string pat = "\"" + key + "\"";
		while (true) {
			size_t p = all.find(pat, start);
			if (p == string::npos) break;
			pos.push_back(p);
			start = p + pat.size();
		}
		return pos;
		};

	// stopOnEmptyFirstColumn
	{
		auto ps = findStr("stopOnEmptyFirstColumn");
		if (!ps.empty()) {
			size_t p = all.find(':', ps[0]);
			if (p != string::npos) {
				size_t q = all.find_first_not_of(" \t\r\n", p + 1);
				if (q != string::npos) {
					if (all.compare(q, 4, "true") == 0) cfg.stopOnEmptyFirstColumn = true;
					else if (all.compare(q, 5, "false") == 0) cfg.stopOnEmptyFirstColumn = false;
				}
			}
		}
	}

	// sheets
	// 아주 러프하게 "sheets" 오브젝트 블록 추출
	size_t psheets = all.find("\"sheets\"");
	if (psheets == string::npos) return true; // 없으면 기본 사용
	size_t colon = all.find(':', psheets);
	if (colon == string::npos) return true;
	size_t braceOpen = all.find('{', colon);
	if (braceOpen == string::npos) return true;
	int depth = 1;
	size_t i = braceOpen + 1;
	while (i < all.size() && depth>0) {
		if (all[i] == '{') depth++;
		else if (all[i] == '}') depth--;
		++i;
	}
	if (depth != 0) return true;
	size_t braceClose = i - 1;
	string body = all.substr(braceOpen + 1, braceClose - braceOpen - 1);

	// 시트 엔트리: "Item": { ... }
	// 매우 단순 파싱
	size_t pos = 0;
	while (true) {
		size_t k1 = body.find('"', pos);
		if (k1 == string::npos) break;
		size_t k2 = body.find('"', k1 + 1);
		if (k2 == string::npos) break;
		string sheetName = body.substr(k1 + 1, k2 - k1 - 1);
		size_t colon2 = body.find(':', k2);
		if (colon2 == string::npos) break;
		size_t ob = body.find('{', colon2);
		if (ob == string::npos) break;
		int d = 1; size_t j = ob + 1;
		while (j < body.size() && d>0) { if (body[j] == '{') d++; else if (body[j] == '}') d--; ++j; }
		if (d != 0) break;
		size_t cb = j - 1;
		string sb = body.substr(ob + 1, cb - ob - 1);

		SheetConf sc;

		// startCell
		{
			size_t p = sb.find("\"startCell\"");
			if (p != string::npos) {
				size_t c = sb.find(':', p);
				size_t q1 = sb.find('"', c);
				size_t q2 = (q1 == string::npos) ? string::npos : sb.find('"', q1 + 1);
				if (q1 != string::npos && q2 != string::npos) sc.startCell = sb.substr(q1 + 1, q2 - q1 - 1);
			}
		}
		// columns
		{
			size_t p = sb.find("\"columns\"");
			if (p != string::npos) {
				size_t c = sb.find(':', p);
				size_t b1 = sb.find('[', c);
				size_t b2 = (b1 == string::npos) ? string::npos : sb.find(']', b1);
				if (b1 != string::npos && b2 != string::npos) {
					string arr = sb.substr(b1 + 1, b2 - b1 - 1);
					size_t u = 0;
					while (true) {
						size_t s1 = arr.find('"', u);
						if (s1 == string::npos) break;
						size_t s2 = arr.find('"', s1 + 1);
						if (s2 == string::npos) break;
						sc.columns.push_back(arr.substr(s1 + 1, s2 - s1 - 1));
						u = s2 + 1;
					}
				}
			}
		}

		cfg.sheets[sheetName] = sc;
		pos = cb + 1;
	}

	return true;
}

// -------------------- CSV → JSON 변환 --------------------
struct Table {
	vector<vector<string>> cells; // [row][col]
};

bool loadCsv(const fs::path& file, Table& t, const std::string& inputEnc /*= "auto"*/) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return false;

	// 파일 전체를 한 번에 읽어 판별 (간단/안전)
	std::string bin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	// \r\n → \n 정규화
	if (!bin.empty()) {
		// BOM 제거(있으면)
		strip_utf8_bom(bin);
	}
	std::string text = bin;

	// 인코딩 결정
	std::string mode = inputEnc;
	if (mode == "auto") {
		// 순서: UTF-8로 보이면 그대로 → 아니면 Windows에서 CP949로 변환 시도
		if (!text.empty() && looks_like_utf8(text)) {
			mode = "utf8";
		}
		else {
#ifdef _WIN32
			mode = "cp949";
#else
			// 비윈도우는 CP949 변환 루틴이 없으므로, 그냥 UTF-8로 가정
			mode = "utf8";
#endif
		}
	}

#ifdef _WIN32
	if (mode == "cp949") {
		text = cp_to_utf8(text, 949); // EUC-KR/CP949
	}
#endif
	// 여기까지 오면 text는 UTF-8

	// 라인 분리
	std::istringstream iss(text);
	std::string line;
	while (std::getline(iss, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		t.cells.push_back(parseCsvLine(line));
	}
	return true;
}


vector<unordered_map<string, string>> sliceTable(
	const Table& t, const SheetConf& sc, bool stopOnEmptyFirstCol
) {
	CellPos st{ 0,0 };
	a1ToRowCol(sc.startCell, st);

	vector<unordered_map<string, string>> rows;

	for (size_t r = st.row; r < t.cells.size(); ++r) {
		const auto& row = t.cells[r];
		// 첫 컬럼 기준 종료 조건
		if (stopOnEmptyFirstCol) {
			string first = (st.col < row.size()) ? trim(row[st.col]) : "";
			if (first.empty()) break;
		}
		unordered_map<string, string> obj;
		bool allEmpty = true;
		for (size_t c = 0; c < sc.columns.size(); ++c) {
			string val;
			size_t col = st.col + c;
			if (col < row.size()) val = trim(row[col]);
			if (!val.empty()) allEmpty = false;
			obj[sc.columns[c]] = val;
		}
		if (allEmpty) {
			if (stopOnEmptyFirstCol) break;
			else continue;
		}
		rows.push_back(std::move(obj));
	}
	return rows;
}

string toJson(const vector<unordered_map<string, string>>& rows) {
	vector<string> arr;
	arr.reserve(rows.size());
	for (const auto& r : rows) {
		vector<pair<string, string>> kv;
		kv.reserve(r.size());
		for (const auto& [k, v] : r) {
			kv.push_back({ k, jsonString(v) });
		}
		// 정렬하면 출력이 안정적
		sort(kv.begin(), kv.end(), [](auto& a, auto& b) {return a.first < b.first; });
		arr.push_back(jsonObject(kv));
	}
	return jsonArray(arr);
}

// -------------------- 메인 --------------------
int main(int argc, char** argv) {
	ios::sync_with_stdio(false);
	cin.tie(nullptr);

	if (argc < 3) {
		cerr << "Usage: " << argv[0] << " <input_dir> <output_dir> [config.json]\n";
		return 1;
	}
	fs::path inputDir = argv[1];
	fs::path outputDir = argv[2];
	fs::path configPath;
	if (argc >= 4) configPath = argv[3];

	if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
		cerr << "Input dir not found: " << inputDir << "\n";
		return 1;
	}
	if (!fs::exists(outputDir)) {
		try { fs::create_directories(outputDir); }
		catch (const std::exception& e) {
			cerr << "Cannot create output dir: " << e.what() << "\n";
			return 1;
		}
	}

	Config cfg;
	// 기본 설정(예시). config.json이 있으면 덮어씌움.
	cfg.sheets = {
		{"Item", SheetConf{ "A2", {"Idx","Name","Type","Value","Effect"} }},
		{"Shop", SheetConf{ "A2", {"ShopId","ItemIdx","Price","Stock"} }}
	};
	loadConfigJson(configPath, cfg);

	// 입력 폴더의 *.csv 반복
	for (auto& entry : fs::directory_iterator(inputDir)) {
		if (!entry.is_regular_file()) continue;
		auto p = entry.path();
		if (p.extension() != ".csv") continue;

		string sheetName = p.stem().string(); // "Item.csv" -> "Item"
		auto it = cfg.sheets.find(sheetName);
		if (it == cfg.sheets.end()) {
			// 설정에 없으면 스킵(원하면 기본 규칙으로 처리하도록 바꿀 수 있음)
			cerr << "[Skip] No config for sheet: " << sheetName << "\n";
			continue;
		}

		Table t;
		if (!loadCsv(p, t, cfg.inputEncoding)) {
			cerr << "[Error] Failed to read: " << p << "\n";
			continue;
		}
		auto rows = sliceTable(t, it->second, cfg.stopOnEmptyFirstColumn);
		string j = toJson(rows);

		fs::path outFile = outputDir / (sheetName + ".json");
		ofstream out(outFile);
		if (!out) {
			cerr << "[Error] Cannot write: " << outFile << "\n";
			continue;
		}

		if (cfg.outputUtf8Bom) {
			const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
			out.write(reinterpret_cast<const char*>(bom), 3);
		}
		out << j << "\n";
		cerr << "[OK] " << sheetName << " -> " << outFile << " (" << rows.size() << " rows)\n";
	}

	return 0;
}
