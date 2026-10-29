// title_db.h
#ifndef TITLE_DB
#define TITLE_DB

#include <Arduino.h>  // byteやuint8_t型などを使う場合
#include <vector>
#include <ArduinoJson.h>  // 7.4.3
#include <LittleFS.h>

struct DiskInfo {
  String diskTitle;
  std::vector<String> trackTitles;
  bool isFound = false;
};

const char* FILE_PATH = "/title_db.json";

#endif