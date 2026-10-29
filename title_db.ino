#include "title_db.h"

bool loadDatabase(JsonDocument& doc) {
  if (!LittleFS.exists(FILE_PATH)) {
    doc.to<JsonObject>();  // ファイルがない場合は空のオブジェクトを作成
    return true;
  }

  File file = LittleFS.open(FILE_PATH, "r");
  if (!file) {
    Serial.println("[ERROR] ファイルの読み込みオープンに失敗しました。");
    return false;
  }

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("[ERROR] JSONのパースに失敗しました: ");
    Serial.println(error.c_str());
    return false;
  }
  return true;
}

bool saveDatabase(JsonDocument& doc) {
  File file = LittleFS.open(FILE_PATH, "w");
  if (!file) {
    Serial.println("[ERROR] ファイルの書き込みオープンに失敗しました。");
    return false;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("[ERROR] ファイルへの書き込みに失敗しました。");
    file.close();
    return false;
  }

  file.close();
  return true;
}

DiskInfo getDiskInfo(const char* diskId) {
  DiskInfo result;
  JsonDocument doc;

  if (!loadDatabase(doc)) return result;

  JsonVariant diskData = doc[diskId];
  if (!diskData.is<JsonObject>()) {
    return result;  // キーがない、またはオブジェクトでない場合は isFound = false で返す
  }

  result.diskTitle = diskData["disk_title"].as<String>();

  JsonArray tracks = diskData["tracks"];
  for (JsonVariant track : tracks) {
    if (track.as<String>() == "") {
      break;
    }
    result.trackTitles.push_back(track.as<String>());
  }

  result.isFound = true;
  return result;
}

bool saveOrUpdateDiskInfo(const char* diskId, const DiskInfo& info) {
  JsonDocument doc;
  if (!loadDatabase(doc)) return false;

  // 新規または上書き用のオブジェクトを作成
  JsonObject diskData = doc[diskId].to<JsonObject>();
  Serial.printf("Disk Id: %s\n", diskId);
  diskData["disk_title"] = info.diskTitle;
  Serial.printf("Disk Title: %s\n", info.diskTitle.c_str());

  JsonArray tracks = diskData["tracks"].to<JsonArray>();
  for (const auto& track : info.trackTitles) {
    if (track == "") {
      break;
    }
    tracks.add(track);
    Serial.printf("Track Title: %s\n", track.c_str());
  }
  if (saveDatabase(doc) == true) {
    Serial.printf("[INFO] DiskID: %s のタイトル情報を保存しました。\n", diskId);
    return true;
  }
  Serial.printf("[ERROR] DiskID: %s のタイトル情報を保存できませんでした。\n", diskId);
  return false;
}

bool deleteDiskInfo(const char* diskId) {
  JsonDocument doc;
  if (!loadDatabase(doc)) return false;

  if (!doc.containsKey(diskId)) {
    Serial.printf("[WARN] DiskID: %s は存在しないため削除できません。\n", diskId);
    return false;
  }

  // キーの削除
  doc.remove(diskId);
  if (saveDatabase(doc) == true) {
    Serial.printf("[INFO] DiskID: %s のタイトル情報を削除しました。\n", diskId);
    return true;
  }
  Serial.printf("[ERROR] DiskID: %s のタイトル情報を削除できませんでした。\n", diskId);
  return false;
}
