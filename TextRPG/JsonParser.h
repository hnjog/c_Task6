#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <cctype>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

// --------- JSON Value ----------
struct JsonValue {
	enum Type { Null, Bool, Number, String, Array, Object };
	Type type = Null;
	double number = 0.0;
	bool boolean = false;
	std::string str;
	std::vector<JsonValue> arr;
	std::unordered_map<std::string, JsonValue> obj;

	static JsonValue makeNull() { return JsonValue(); }
	static JsonValue makeBool(bool b) { JsonValue v; v.type = Bool; v.boolean = b; return v; }
	static JsonValue makeNumber(double d) { JsonValue v; v.type = Number; v.number = d; return v; }
	static JsonValue makeString(const std::string& s) { JsonValue v; v.type = String; v.str = s; return v; }
	static JsonValue makeArray() { JsonValue v; v.type = Array; return v; }
	static JsonValue makeObject() { JsonValue v; v.type = Object; return v; }

	const JsonValue* get(const std::string& key) const {
		if (type != Object) return nullptr;
		std::unordered_map<std::string, JsonValue>::const_iterator it = obj.find(key);
		return it == obj.end() ? nullptr : &it->second;
	}
	std::string getString(const std::string& key, const std::string& def = "") const {
		const JsonValue* p = get(key); return (p && p->type == String) ? p->str : def;
	}
	int getInt(const std::string& key, int def = 0) const {
		const JsonValue* p = get(key); return (p && p->type == Number) ? (int)(p->number) : def;
	}
};

// --------- Minimal JSON Parser (subset) ----------
struct JsonParser {
	const std::string& s; size_t i, n;
	explicit JsonParser(const std::string& src) : s(src), i(0), n(src.size()) {}

	void skipWs() { while (i < n && std::isspace((unsigned char)s[i])) ++i; }
	bool match(char c) { skipWs(); if (i < n && s[i] == c) { ++i; return true; } return false; }
	void expect(char c) { skipWs(); if (i >= n || s[i] != c) throw std::runtime_error(std::string("expected '") + c + "'"); ++i; }

	JsonValue parse() {
		skipWs(); JsonValue v = parseValue(); skipWs();
		if (i != n) throw std::runtime_error("extra characters after JSON");
		return v;
	}

	JsonValue parseValue() {
		skipWs(); if (i >= n) throw std::runtime_error("unexpected end");
		char c = s[i];
		if (c == '{') return parseObject();
		if (c == '[') return parseArray();
		if (c == '"') return parseString();
		if (c == 't' || c == 'f') return parseBool();
		if (c == 'n') return parseNull();
		if (c == '-' || std::isdigit((unsigned char)c)) return parseNumber();
		throw std::runtime_error(std::string("unexpected char: ") + c);
	}

	JsonValue parseObject() {
		expect('{');
		JsonValue v = JsonValue::makeObject();
		skipWs();
		if (match('}')) return v;
		while (true) {
			skipWs(); if (s[i] != '"') throw std::runtime_error("object key must be string");
			std::string key = parseString().str;
			skipWs(); expect(':');
			JsonValue val = parseValue();
			v.obj.insert(std::make_pair(key, val));
			skipWs();
			if (match('}')) break;
			expect(',');
		}
		return v;
	}

	JsonValue parseArray() {
		expect('[');
		JsonValue v = JsonValue::makeArray();
		skipWs();
		if (match(']')) return v;
		while (true) {
			v.arr.push_back(parseValue());
			skipWs();
			if (match(']')) break;
			expect(',');
		}
		return v;
	}

	JsonValue parseString() {
		expect('"');
		std::string out; out.reserve(32);
		while (i < n) {
			char c = s[i++];
			if (c == '"') break;
			if (c == '\\') {
				if (i >= n) throw std::runtime_error("bad escape");
				char e = s[i++];
				switch (e) {
				case '"': out.push_back('"'); break;
				case '\\': out.push_back('\\'); break;
				case '/': out.push_back('/'); break;
				case 'b': out.push_back('\b'); break;
				case 'f': out.push_back('\f'); break;
				case 'n': out.push_back('\n'); break;
				case 'r': out.push_back('\r'); break;
				case 't': out.push_back('\t'); break;
				default: throw std::runtime_error("unsupported escape (\\uXXXX omitted)");
				}
			}
			else {
				out.push_back(c);
			}
		}
		return JsonValue::makeString(out);
	}

	JsonValue parseBool() {
		if (i + 3 < n && s.compare(i, 4, "true") == 0) { i += 4; return JsonValue::makeBool(true); }
		if (i + 4 < n && s.compare(i, 5, "false") == 0) { i += 5; return JsonValue::makeBool(false); }
		throw std::runtime_error("bad boolean");
	}

	JsonValue parseNull() {
		if (i + 3 < n && s.compare(i, 4, "null") == 0) { i += 4; return JsonValue::makeNull(); }
		throw std::runtime_error("bad null");
	}

	JsonValue parseNumber() {
		size_t start = i;
		if (s[i] == '-') ++i;
		if (i < n && s[i] == '0') { ++i; }
		else {
			if (i >= n || !std::isdigit((unsigned char)s[i])) throw std::runtime_error("bad number");
			while (i < n && std::isdigit((unsigned char)s[i])) ++i;
		}
		if (i < n && s[i] == '.') {
			++i; if (i >= n || !std::isdigit((unsigned char)s[i])) throw std::runtime_error("bad number frac");
			while (i < n && std::isdigit((unsigned char)s[i])) ++i;
		}
		if (i < n && (s[i] == 'e' || s[i] == 'E')) {
			++i; if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
			if (i >= n || !std::isdigit((unsigned char)s[i])) throw std::runtime_error("bad number exp");
			while (i < n && std::isdigit((unsigned char)s[i])) ++i;
		}
		double val = std::strtod(s.substr(start, i - start).c_str(), nullptr);
		return JsonValue::makeNumber(val);
	}
};
