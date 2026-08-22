#pragma once

// Minimal, self-contained JSON value type: parser + serializer.
// Written in-house to keep the project's zero-external-dependency rule
// (this repo previously referenced <nlohmann/json.hpp> in a few headers
// without ever vendoring it, so nothing that used it could actually build).

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>

namespace jsonlite {

class Json {
public:
  enum class Type { Null, Boolean, Number, String, Array, Object };

  Json() : type_(Type::Null) {}
  Json(std::nullptr_t) : type_(Type::Null) {}
  Json(bool b) : type_(Type::Boolean), bool_(b) {}
  Json(int n) : type_(Type::Number), num_(n) {}
  Json(long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(long long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(double n) : type_(Type::Number), num_(n) {}
  Json(const char* s) : type_(Type::String), str_(s) {}
  Json(const std::string& s) : type_(Type::String), str_(s) {}

  static Json object() { Json j; j.type_ = Type::Object; return j; }
  static Json array() { Json j; j.type_ = Type::Array; return j; }

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_string() const { return type_ == Type::String; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_bool() const { return type_ == Type::Boolean; }
  bool is_object() const { return type_ == Type::Object; }
  bool is_array() const { return type_ == Type::Array; }

  // Object access: auto-vivifies to an object on non-const access, like nlohmann.
  Json& operator[](const std::string& key) {
    if (type_ == Type::Null) type_ = Type::Object;
    if (type_ != Type::Object) throw std::runtime_error("Json::operator[] on non-object");
    for (auto& kv : obj_) {
      if (kv.first == key) return *kv.second;
    }
    obj_.emplace_back(key, std::make_shared<Json>());
    return *obj_.back().second;
  }

  const Json& at(const std::string& key) const {
    if (type_ != Type::Object) throw std::runtime_error("Json::at() on non-object");
    for (auto& kv : obj_) {
      if (kv.first == key) return *kv.second;
    }
    throw std::runtime_error("Json::at() key not found: " + key);
  }

  bool contains(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (auto& kv : obj_) {
      if (kv.first == key) return true;
    }
    return false;
  }

  Json& operator[](size_t idx) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) throw std::runtime_error("Json::operator[] on non-array");
    while (arr_.size() <= idx) arr_.push_back(std::make_shared<Json>());
    return *arr_[idx];
  }

  void push_back(const Json& value) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) throw std::runtime_error("Json::push_back() on non-array");
    arr_.push_back(std::make_shared<Json>(value));
  }

  size_t size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
  }

  // Accessors with safe fallbacks.
  std::string as_string(const std::string& fallback = "") const {
    return type_ == Type::String ? str_ : fallback;
  }
  double as_double(double fallback = 0.0) const {
    return type_ == Type::Number ? num_ : fallback;
  }
  long as_long(long fallback = 0) const {
    return type_ == Type::Number ? static_cast<long>(num_) : fallback;
  }
  bool as_bool(bool fallback = false) const {
    return type_ == Type::Boolean ? bool_ : fallback;
  }

  const std::vector<std::shared_ptr<Json>>& items() const { return arr_; }
  const std::vector<std::pair<std::string, std::shared_ptr<Json>>>& members() const { return obj_; }

  static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
          } else {
            out += static_cast<char>(c);
          }
      }
    }
    return out;
  }

  std::string dump() const {
    std::ostringstream ss;
    dump_to(ss);
    return ss.str();
  }

  static Json parse(const std::string& text) {
    size_t pos = 0;
    Json result = parse_value(text, pos);
    return result;
  }

private:
  Type type_;
  bool bool_ = false;
  double num_ = 0.0;
  std::string str_;
  std::vector<std::shared_ptr<Json>> arr_;
  std::vector<std::pair<std::string, std::shared_ptr<Json>>> obj_;

  void dump_to(std::ostringstream& ss) const {
    switch (type_) {
      case Type::Null: ss << "null"; break;
      case Type::Boolean: ss << (bool_ ? "true" : "false"); break;
      case Type::Number: {
        if (num_ == static_cast<long long>(num_)) {
          ss << static_cast<long long>(num_);
        } else {
          ss << num_;
        }
        break;
      }
      case Type::String: ss << '"' << escape(str_) << '"'; break;
      case Type::Array: {
        ss << '[';
        for (size_t i = 0; i < arr_.size(); ++i) {
          if (i > 0) ss << ',';
          arr_[i]->dump_to(ss);
        }
        ss << ']';
        break;
      }
      case Type::Object: {
        ss << '{';
        for (size_t i = 0; i < obj_.size(); ++i) {
          if (i > 0) ss << ',';
          ss << '"' << escape(obj_[i].first) << "\":";
          obj_[i].second->dump_to(ss);
        }
        ss << '}';
        break;
      }
    }
  }

  static void skip_ws(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
  }

  static Json parse_value(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    if (pos >= s.size()) return Json();
    char c = s[pos];
    if (c == '{') return parse_object(s, pos);
    if (c == '[') return parse_array(s, pos);
    if (c == '"') return Json(parse_string(s, pos));
    if (c == 't' || c == 'f') return parse_bool(s, pos);
    if (c == 'n') { pos += 4; return Json(); }
    return parse_number(s, pos);
  }

  static Json parse_object(const std::string& s, size_t& pos) {
    Json j = Json::object();
    pos++; // {
    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == '}') { pos++; return j; }
    while (pos < s.size()) {
      skip_ws(s, pos);
      std::string key = parse_string(s, pos);
      skip_ws(s, pos);
      if (pos < s.size() && s[pos] == ':') pos++;
      Json value = parse_value(s, pos);
      j[key] = value;
      skip_ws(s, pos);
      if (pos < s.size() && s[pos] == ',') { pos++; continue; }
      if (pos < s.size() && s[pos] == '}') { pos++; break; }
      break;
    }
    return j;
  }

  static Json parse_array(const std::string& s, size_t& pos) {
    Json j = Json::array();
    pos++; // [
    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == ']') { pos++; return j; }
    while (pos < s.size()) {
      Json value = parse_value(s, pos);
      j.push_back(value);
      skip_ws(s, pos);
      if (pos < s.size() && s[pos] == ',') { pos++; continue; }
      if (pos < s.size() && s[pos] == ']') { pos++; break; }
      break;
    }
    return j;
  }

  static std::string parse_string(const std::string& s, size_t& pos) {
    std::string out;
    if (pos >= s.size() || s[pos] != '"') return out;
    pos++; // opening quote
    while (pos < s.size() && s[pos] != '"') {
      char c = s[pos];
      if (c == '\\' && pos + 1 < s.size()) {
        char next = s[pos + 1];
        switch (next) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (pos + 5 < s.size()) {
              std::string hex = s.substr(pos + 2, 4);
              unsigned int code = static_cast<unsigned int>(strtoul(hex.c_str(), nullptr, 16));
              // Basic BMP-only UTF-8 encoding (no surrogate pair handling).
              if (code < 0x80) {
                out += static_cast<char>(code);
              } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
              } else {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
              }
              pos += 4;
            }
            break;
          }
          default: out += next;
        }
        pos += 2;
      } else {
        out += c;
        pos++;
      }
    }
    if (pos < s.size()) pos++; // closing quote
    return out;
  }

  static Json parse_bool(const std::string& s, size_t& pos) {
    if (s.compare(pos, 4, "true") == 0) { pos += 4; return Json(true); }
    if (s.compare(pos, 5, "false") == 0) { pos += 5; return Json(false); }
    return Json();
  }

  static Json parse_number(const std::string& s, size_t& pos) {
    size_t start = pos;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
    while (pos < s.size() &&
           (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' ||
            s[pos] == 'e' || s[pos] == 'E' || s[pos] == '-' || s[pos] == '+')) {
      pos++;
    }
    if (pos == start) { pos++; return Json(); }
    return Json(std::atof(s.substr(start, pos - start).c_str()));
  }
};

} // namespace jsonlite

using json = jsonlite::Json;
