// Minimalna imitacja Arduino.h do testów parsera na PC (g++ / clang).
// Odwzorowuje tylko te elementy klasy String, których używa aprs_parser.cpp.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#define PROGMEM

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  explicit String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned int v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(float v, int dec = 2) { fmt(v, dec); }
  String(double v, int dec = 2) { fmt(v, dec); }

  unsigned int length() const { return s_.size(); }
  const char *c_str() const { return s_.c_str(); }
  void reserve(unsigned int n) { s_.reserve(n); }

  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }

  String substring(unsigned int from) const { return substring(from, s_.size()); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = s_.size();
    return String(s_.substr(from, to - from));
  }

  int indexOf(char c, unsigned int from = 0) const { return find(std::string(1, c), from); }
  int indexOf(const String &str, unsigned int from = 0) const { return find(str.s_, from); }
  int lastIndexOf(char c) const {
    size_t p = s_.rfind(c);
    return p == std::string::npos ? -1 : (int)p;
  }

  bool startsWith(const String &p) const { return s_.compare(0, p.s_.size(), p.s_) == 0 && s_.size() >= p.s_.size(); }
  bool endsWith(const String &p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }

  long toInt() const { return atol(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }

  void trim() {
    size_t b = 0, e = s_.size();
    while (b < e && isspace((unsigned char)s_[b])) b++;
    while (e > b && isspace((unsigned char)s_[e - 1])) e--;
    s_ = s_.substr(b, e - b);
  }
  void toUpperCase() { for (auto &c : s_) c = toupper((unsigned char)c); }
  void toLowerCase() { for (auto &c : s_) c = tolower((unsigned char)c); }

  void replace(char from, char to) { for (auto &c : s_) if (c == from) c = to; }
  void replace(const String &from, const String &to) {
    if (from.s_.empty()) return;
    size_t p = 0;
    while ((p = s_.find(from.s_, p)) != std::string::npos) {
      s_.replace(p, from.s_.size(), to.s_);
      p += to.s_.size();
    }
  }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }

  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator==(const char *o) const { return s_ == o; }
  bool operator!=(const char *o) const { return s_ != o; }

  const std::string &str() const { return s_; }

 private:
  std::string s_;
  int find(const std::string &what, unsigned int from) const {
    if (from >= s_.size()) return -1;
    size_t p = s_.find(what, from);
    return p == std::string::npos ? -1 : (int)p;
  }
  void fmt(double v, int dec) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", dec, v);
    s_ = buf;
  }
};

inline String operator+(const String &a, const String &b) { String r = a; r += b; return r; }
inline String operator+(const String &a, const char *b) { String r = a; r += b; return r; }
inline String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, char b) { String r = a; r += b; return r; }
