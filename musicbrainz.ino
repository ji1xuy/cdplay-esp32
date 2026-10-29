#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <mbedtls/sha1.h>
#include <base64.h>       // ESP32コア標準の公式Base64ライブラリ
#include <ArduinoJson.h>  // 7.4.3

String encodeMusicBrainzBase64(const uint8_t* input, size_t length) {
  // ESP32公式の確実な関数でBase64文字列に変換
  String result = base64::encode(input, length);

  // 改行コードなどが混入した場合は除去
  result.trim();

  // MusicBrainzのURL安全な仕様（独自Base64）に文字を置換
  result.replace('+', '.');
  result.replace('/', '_');
  result.replace('=', '-');

  return result;
}

// 1. Disc IDの計算関数
String calculateDiscId(const uint8_t firstTrack, const uint8_t lastTrack,
                       const uint32_t leadOutLBA, const uint32_t* trackLBA) {
  char header[32];
  // 仕様(Red Bookなど)通り、リードアウトLBAに150を足す
  // 最初のトラックの開始位置:LBA=0(MSFでは 00:02:00）75フレーム/秒X2秒=150
  sprintf(header, "%02X %02X %08X", firstTrack, lastTrack, leadOutLBA + 150);

  String trackStr = "";
  for (int i = 1; i <= 99; i++) {
    char t[16];
    if (i >= firstTrack && i <= lastTrack) {
      sprintf(t, " %08X", trackLBA[i] + 150);
    } else {
      sprintf(t, " 00000000");
    }
    trackStr += t;
  }

  String mb_fullInput = String(header) + trackStr;

  // --- ESP32 コア v3.x 標準の sha1 処理 ---
  unsigned char sha1_res_mb[20];
  mbedtls_md_context_t md_ctx_mb;
  mbedtls_md_init(&md_ctx_mb);
  mbedtls_md_setup(&md_ctx_mb, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 0);
  mbedtls_md_starts(&md_ctx_mb);
  mbedtls_md_update(&md_ctx_mb, (const unsigned char*)mb_fullInput.c_str(), mb_fullInput.length());
  mbedtls_md_finish(&md_ctx_mb, sha1_res_mb);
  mbedtls_md_free(&md_ctx_mb);

  return encodeMusicBrainzBase64(sha1_res_mb, 20);
}

// UTF-8のバイト列からUnicodeコードポイントを1文字ずつデコードする関数
int getUtf8Char(String& str, int& index) {
  if (index >= str.length()) return 0;

  uint8_t c1 = str[index++];
  if ((c1 & 0x80) == 0) return c1;  // 1バイト文字 (ASCII)

  if ((c1 & 0xE0) == 0xC0) {  // 2バイト文字
    if (index >= str.length()) return 0;
    uint8_t c2 = str[index++];
    return ((c1 & 0x1F) << 6) | (c2 & 0x3F);
  }

  if ((c1 & 0xF0) == 0xE0) {  // 3バイト文字（日本語の多くはここ）
    if (index + 1 >= str.length()) return 0;
    uint8_t c2 = str[index++];
    uint8_t c3 = str[index++];
    return ((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
  }

  return 0;  // 4バイト以上は省略
}

// 漢字を取り除く関数
String removeKanji(String str) {
  String result = ""; 
  int i = 0;

  while (i < str.length()) {
    int startIdx = i;                     // 文字の開始位置を記録
    int codePoint = getUtf8Char(str, i);  // 1文字進めてコードポイントを取得
    /*
    // 漢字のUnicode範囲 (主要な CJK統合漢字: U+4E00 ～ U+9FFF)
    if (codePoint >= 0x4E00 && codePoint <= 0x9FFF) {
      // 漢字は追加せずスキップ
      continue;
    }
    if (codePoint >= 0x0400 && codePoint <= 0x04FF) {
      // ロシア語（キリル文字）は追加せずスキップ
      continue;
    }
    */
    switch (codePoint) {
      case 0x2019: result += "'"; continue; // UNICODEのカンマ(’):0x2019 ⇒ ASCIIのカンマ(')に変換
      case 0x2013: result += "-"; continue; // UNICODEのハイフン(–):0x2013 ⇒ASCIIのハイフン (-)に変換
      default: break;
    } 
    if (codePoint > 0x00FF) {
      // ASCIIコード以外は追加せずスキップ
      continue;
    }

    // 漢字以外（ひらがな、カタカナ、英数など）はそのまま結果に追加
    result += str.substring(startIdx, i);
  }
  return result;
}

bool parseMusicBrainzJson(Stream& stream, const uint8_t lastTrack) {
  // 1. 【v7配列仕様】必要なキーだけを抽出するための「構造化フィルター」を定義
  JsonDocument filter;

  // releasesが「オブジェクトの配列」であることを明示的に定義します
  JsonObject filter_release = filter["releases"].to<JsonArray>().add<JsonObject>();
  filter_release["title"] = true;
  filter_release["artist-credit"][0]["name"] = true;  // artist-creditも配列構造

  // media -> tracks も配列構造として正しく定義
  JsonObject filter_track = filter_release["media"][0]["tracks"].to<JsonArray>().add<JsonObject>();
  filter_track["title"] = true;
  filter_track["number"] = true;

  JsonDocument doc;

  // 通信を少しずつ読み込みながらフィルターを適用しパース
  DeserializationError error = deserializeJson(
    doc,
    stream,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(20));

  if (error) {
    Serial.printf("[JSON] パース失敗: %s\n", error.c_str());
    return false;
  }

  // 変数の初期化
  for (String& title : cddbTrackTitles) title = "";
  int cddbTrackCount = 0;

  // 3. JSONの配列構造に合わせて、タイトル情報を全件取得
  JsonArray releases = doc["releases"].as<JsonArray>();
  cddbCandidateCount = releases.size();
  Serial.printf("CDDB登録件数: %d件\n", cddbCandidateCount);
  cddbCandidateCount = min((int)cddbCandidateCount, (int)CDDB_MAX_CANDIDATES);
  if (cddbCandidateCount > 0) {
    String trackTitles[99];
    for (int i = 0; i < cddbCandidateCount; i++) {
      JsonObject release = releases[i].as<JsonObject>();

      cddbCandidates[i].title = release["title"] ? removeKanji(String(release["title"])) : "Unknown Album";

      cddbCandidates[i].artist = String(release["artist-credit"][0]["name"]);

      JsonArray medias = release["media"].as<JsonArray>();
      if (medias.size() > 0) {
        JsonArray tracks = medias[0]["tracks"].as<JsonArray>();
        cddbTrackCount = min((int)tracks.size(), 99);
        for (JsonObject track : tracks) {
          int trackNum = track["number"].as<int>();
          const char* trackTitle = track["title"];
          if (trackNum >= 1 && trackNum <= lastTrack && trackNum < 100 && trackTitle) {
            trackTitles[trackNum] = String(trackTitle);
            cddbCandidates[i].titles[trackNum] = removeKanji(trackTitles[trackNum]);
          }
        }
      }
      // 1件目の候補だけシリアルに出力
      if (i == 0) {
        Serial.printf("====== タイトル情報 (%d件目) ======\n", i + 1);
        cddbDiscTitle = cddbCandidates[0].title;
        if (cddbDiscTitle != "") Serial.printf("Album: %s\n", cddbDiscTitle.c_str());
        const char* artist = cddbCandidates[0].artist.c_str();
        if (artist) Serial.printf("Artist: %s\n", artist);
        Serial.println("-----------------------------------");
        for (int trackNum = 1; trackNum <= cddbTrackCount; trackNum++) {
          cddbTrackTitles[trackNum] = cddbCandidates[i].titles[trackNum];
          Serial.printf("Track %02d: %s\n", trackNum, trackTitles[trackNum].c_str());
        }
        Serial.println("===================================\n");
      }
    }
    cddbCandidatesReady = true;
    return true;
  } else {
    Serial.println("[JSON] 該当するタイトル情報が配列内に見つかりませんでした。");
    return false;
  }
}

CddbStatus fetchMusicBrainzCDInfo(const String discId, const uint8_t firstTrack, const uint8_t lastTrack,
                                  const uint32_t leadOutLBA, const uint32_t* trackLBA) {
  CddbStatus retCode;

  if (WiFi.status() != WL_CONNECTED) return CddbStatus::NetworkError;

  WiFiClient client;
  HTTPClient http;

  // 【1回目】Disc IDで検索
  String url = "http://musicbrainz.org/ws/2/discid/" + discId + "?fmt=json&inc=artists+recordings";

  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(PRGNAME " (" MAILADDRESS ")");

  Serial.println("MusicBrainzへリクエスト中 (Disc ID)...");
  Serial.println(url);

  int httpCode = http.GET();

  // もし404エラー（見つからない）だった場合は、あいまいTOC検索に切り替え
  if (httpCode == 404) {
    Serial.println("➔ Disc ID不一致(404)");
    http.end();  // 一旦切断

    // あいまい検索用のURLパラメータ（10進数のLBAリスト）
    // フォーマット: First+Last+LeadOut+Track1+Track2... （すべて+150する）
    String tocParam = String(firstTrack) + "+" + String(lastTrack) + "+" + String(leadOutLBA + 150);
    for (int i = 1; i <= lastTrack; i++) {
      tocParam += "+" + String(trackLBA[i] + 150);
    }

    // discid の代わりに「-」を指定し、tocパラメータを付与
    url = "http://musicbrainz.org/ws/2/discid/-?fmt=json&inc=artists+recordings&toc=" + tocParam;

    http.begin(client, url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(PRGNAME " (" MAILADDRESS ")");
    Serial.println("MusicBrainzへリクエスト中 (TOC)...");
    Serial.println(url);
    httpCode = http.GET();
  }

  // 最終的な判定
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("➔ タイトル情報取得成功 (200 OK) / JSONパース開始");
    auto streamPtr = http.getStreamPtr();
    retCode = parseMusicBrainzJson(*streamPtr, lastTrack) ? CddbStatus::Ready : CddbStatus::NoMatch;
  } else {
    Serial.printf("HTTP通信エラー: (%d)\n", httpCode);
    retCode = CddbStatus::NetworkError;
  }
  http.end();
  return retCode;
}

static String musicBrainzLookup(void) {
  // 初期化
  cddbCandidatesReady = false;
  cddbDiscTitle = "";
  for (String& title : cddbTrackTitles) title = "";

  // TOCから得られるパラメーターを各変数にセット
  uint8_t firstTrack = 1; // 最初のトラック番号
  uint8_t lastTrack = tocTrackCount; // 総トラック数
  uint32_t leadOutLBA = discLeadOutLba; // 有効なデータの直後に位置するアドレス
  uint32_t trackLBAs[100];
  memset(trackLBAs, 0, sizeof(trackLBAs));
  for (int i = firstTrack; i <= lastTrack; i++) {
    trackLBAs[i] = tocTracks[i - 1].startLba; // 各トラックの開始位置のアドレス
  }

  // 1. ハッシュ計算
  String discId = calculateDiscId(firstTrack, lastTrack, leadOutLBA, trackLBAs);
  mbdbDiscId = discId;

  // 2. DBを検索
  Serial.printf("Disc ID: %s\n", discId.c_str());
  DiskInfo diskInfo = getDiskInfo(discId.c_str());
  if (diskInfo.isFound) {
    cddbCandidates[0].title = diskInfo.diskTitle;
    cddbDiscTitle = diskInfo.diskTitle;
    Serial.printf("➔ タイトルDB(%s)にタイトル情報登録あり\n", FILE_PATH);
    Serial.println("===================================");
    Serial.printf("Album: %s\n", diskInfo.diskTitle.c_str());
    Serial.println("-----------------------------------");
    for (size_t i = 0; i < diskInfo.trackTitles.size(); i++) {
      if (diskInfo.trackTitles[i] == "") {
        break;
      }
      cddbCandidates[0].titles[i + 1] = diskInfo.trackTitles[i];
      cddbTrackTitles[i + 1] = diskInfo.trackTitles[i];
      Serial.printf("Track %02d: %s\n", i + 1, diskInfo.trackTitles[i].c_str());
    }
    Serial.println("===================================\n");
    mbdbDiscIdSaved =  true;
    cddbCandidateCount = 1;
    cddbCandidateSelection = 0;
    cddbCandidatesReady = true;
    cddbStatus = CddbStatus::Ready;
  } else {
    mbdbDiscIdSaved = false;
    Serial.printf("➔ タイトルDB(%s)にタイトル情報登録なし\n", FILE_PATH);
    // 3. タイトルDBに見つからないのでmbdbを検索
    cddbStatus = fetchMusicBrainzCDInfo(discId, firstTrack, lastTrack, leadOutLBA, trackLBAs);
  }
  cddbMetadataReady = cddbStatus == CddbStatus::Ready ? true : false;
  cddbLookupRunning = false;
  return discId;
}
