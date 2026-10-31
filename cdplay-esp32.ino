//このコードの引用元
//cdplay-esp32
//https://github.com/darekasan/cdplay-esp32
#define PRGNAME "cdplay-esp32"
#define VERSION "ver.20261031.00"

/*
・ハードウェア
ボード:
 Amazon ESP32-S3 WROOM FREENOVE 8MBフラッシュメモリ、8MB PSRAM
OLED:
 共立電子 WINSTAR OLEDモジュール WEH001602ARPP5N00100 (パラレル4bitモードで使用)
双方向ロジックレベルコンバーター:
 Amazon　TXS0108E 8チャンネルロジックレベルコンバータ
I2Sコンバーター:
 Amazon DAOKAI PCM5102モジュール、I2S IISロスレスデジタルオーディオDACデコーダーモジュール
 SCKとBCKの間のパッド ショート (オープンのままだと、音声出力はされるが手を近づけるとノイズが入る)
 H1L(FLT)  1-L ショート 
 H2L(DEMP) 2-L ショート 
 H3L(XSMT) 3-H ショート 
 H4L(FMT)  4-L ショート 
IDE(またはSATA)-USB変換ケーブル:
 5Vは、USB-OTGコネクタからではなく別電源から供給する
 CDドライブ
　|
 IDE(またはSATA)-USB変換ケーブル
　|
 片側がUSB-Aメスのケーブル
 USB 白(D-) -> GPIOピン 19 
 USB 緑(D+) -> GPIOピン 20
 USB 黒(GND)-> GND
 USB 赤(5V) -> 別な5V電源(ESP32-S3の5Vには接続しない) (消費電流0.18A)

・ソフトウェア
Arduino IDE 2.3.10
ボードマネージャ: esp32 by Expressif Systems
 バージョン 3.0.0　 動作OK 
 バージョン 3.1.0　 同上
 バージョン 3.2.0　 同上
 バージョン 3.3.0　 同上  　 
 バージョン 3.3.10　同上
 バージョン 3.3.11　USB-OTG: ドライブ認識せず(WATING FOR DRIVEで止まる)
 バージョン 3.3.12　同上
ボード選択: ESP32S3 Dev Module
ボード設定:
USB CDC On Boot: Disabled (=デフォルト)(Enabledにするとリセットの繰り返し)
Flash size: 8MB(64Mb)
Partition Scheme: 8MB with spiffs (3MB APP/1.5MB SPIFFS)
PSRAM: OPI SRAM
USB Mode: Hardware CDC and JTAG または　USB-OTG (TinyUSB)(どちらでもよい) 

・元のコードからの変更点
グラフィックLCDからキャラクタOLED(16文字X2行)に変更
タイトル情報の表示を漢字ではなく1byte文字に変更
　cddbLookupTask()関数　データの取得先を変更(freedbtest.dyndns.org　⇒　musicbrainz.org)
CDDBモードでの機能変更
　ACTION短押し
　　(変更前)
　　　選択した候補を基にCDDB検索を実行
　　(変更後)
　　　タイトルDBに未保存⇒ タイトル情報検索
　　　タイトルDBに保存済⇒ CDDB候補の選択
　　　(タイトルDB: ディスクタイトルと曲タイトルのデータをESP32内蔵のファイルシステムにJSON形式で保存)
　ACTION長押し　<=追加
　　タイトルDBに未保存⇒ DISK IDをキーにしてタイトルDBに保存
　　タイトルDBに保存済⇒ DISK IDをキーにしてタイトルDBの保存データ削除を確認するメッセージ表示
　　　確認メッセージ表示後　
　　　　ACTION短押し -> データを削除して、タイトル情報再検索
　　　　MODE 短押し-> CDDBモードから抜ける
　この変更に併せて、タイトル情報検索の際は、タイトルDBにタイトル情報が保存されていればそのデータを使用して
　なければMusicBrainzへアクセスするように変更
STOPした時にフリーズが頻繁に起きるので、STOP時にvTaskDelay(pdMS_TO_TICKS(5))を挿入
Wi-Fi接続状態表示　(Wi-Fiアイコン):接続済　_:未接続
タイトル情報表示　♪:表示あり　_:表示なし
タイトル情報をESP32内蔵のファイルシステムに保存の有無表示　(ダウンロードアイコン):保存済　_:未保存
シャッフル表示　(シャッフルアイコン):あり　_:なし
リピート表示　(1曲リピートアイコン):1曲　(全曲リピートアイコン):全曲　_:リピートなし

(不具合)
シリアルモニタにBOT phase 2 failed, USB status 1 を出力し固まる時がある
 2倍速設定してから発生なし
*/

#include "title_db.h"
#include <Arduino.h>

#include <LiquidCrystal.h>
#define LCD_JAPANESE 0x00
//WINSTARのOLEDを4bitモードで使用する場合、LiquidCrystalライブラリではウォームスタート時に正常に初期化できない
//(ウォームスタート時はOLEDが4bitモードを維持しているがコードは8bitモード前提で実行されるため)
//LiquidCrystalライブラリからLiquidCrystal_OLEDクラスを作り初期化コードをOLEDで動作するようにする
//Adafruit_CharacterOLEDライブラリはESP32では動作しないので使用不可
class LiquidCrystal_OLED : public LiquidCrystal {
public:
  // 親クラス（LiquidCrystal）のコンストラクタをそのまま引き継ぐ
  using LiquidCrystal::LiquidCrystal;

  uint8_t _enable_pin; // activated by a HIGH pulse.
  uint8_t _data_pins[8];

  uint8_t _displayfunction;
  uint8_t _displaycontrol;
  uint8_t _displaymode;

  //iquidCrystalライブラリ内でprivateで宣言されているためコピーして使用
  void pulseEnable(void) {
    digitalWrite(_enable_pin, LOW);
    delayMicroseconds(1);    
    digitalWrite(_enable_pin, HIGH);
    delayMicroseconds(1);    // enable pulse must be >450ns
    digitalWrite(_enable_pin, LOW);
    delayMicroseconds(100);   // commands need > 37us to settle
  }

  //LiquidCrystalライブラリ内でprivateで宣言されているためコピーして使用
  void write4bits(uint8_t value) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(_data_pins[i], (value >> i) & 0x01);
    }

    pulseEnable();
  }

  void begin(uint8_t cols, uint8_t lines, uint8_t dotsize = LCD_5x8DOTS) {
    // LiquidCrystalライブラリ内でprivateな_numlines変数が表示行数を決めている
    // 2行表示にするためにLiquidCrystal::begin関数を実行する
    LiquidCrystal::begin(cols, lines, dotsize);
    
    if (lines > 1) {
      _displayfunction |= LCD_2LINE;
    }

    // for some 1 line displays you can select a 10 pixel high font
    if ((dotsize != LCD_5x8DOTS) && (lines == 1)) {
      _displayfunction |= LCD_5x10DOTS;
    }

    // SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
    // according to datasheet, we need at least 40ms after power rises above 2.7V
    // before sending commands. Arduino can turn on way before 4.5V so we'll wait 50ms
    delayMicroseconds(50000); 
    
    //put the LCD into 4 bit or 8 bit mode
    if (! (_displayfunction & LCD_8BITMODE)) {
      //Adafruit_CharacterOLED.cppからコピーして一部修正
      // Initialization sequence is not quite as documented by Winstar.
      // Documented sequence only works on initial power-up.  
      // An additional step of putting back into 8-bit mode first is 
      // required to handle a warm-restart.
      //
      // In the data sheet, the timing specs are all zeros(!).  These have been tested to 
      // reliably handle both warm & cold starts.

      // 4-Bit initialization sequence from Technobly
      write4bits(0x03); // Put back into 8-bit mode
      delayMicroseconds(5000);
      write4bits(0x08);
      delayMicroseconds(5000);
      write4bits(0x02); // Put into 4-bit mode
      delayMicroseconds(5000);
      write4bits(0x02);
      delayMicroseconds(5000);
      write4bits(0x08 | LCD_JAPANESE);
      delayMicroseconds(5000);
    
      command(0x08);	// Turn Off
      delayMicroseconds(5000);
      command(0x01);	// Clear Display
      delayMicroseconds(5000);
      command(0x06);	// Set Entry Mode
      delayMicroseconds(5000);
      command(0x02);	// Home Cursor
      delayMicroseconds(5000);
      command(0x17);	// G/C=Character , DCDC Converter ON
      delayMicroseconds(5000);
      command(0x0C);	// Turn On - enable cursor & blink
      delayMicroseconds(5000);
    } else {
      // this is according to the hitachi HD44780 datasheet
      // page 45 figure 23

      // Send function set command sequence
      command(LCD_FUNCTIONSET | _displayfunction);
      delayMicroseconds(4500);  // wait more than 4.1ms

      // second try
      command(LCD_FUNCTIONSET | _displayfunction);
      delayMicroseconds(150);

      // third go
      command(LCD_FUNCTIONSET | _displayfunction);
    }

    // finally, set # lines, font size, etc.
    command(LCD_FUNCTIONSET | _displayfunction);  

    // turn the display on with no cursor or blinking default
    _displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;  
    display();

    // clear it off
    clear();

    // Initialize to default text direction (for romance languages)
    _displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    // set the entry mode
    command(LCD_ENTRYMODESET | _displaymode);

  }
};
static constexpr int Rs_pin = 9;
static constexpr int Rw_pin = 3;
static constexpr int En_pin = 8;
static constexpr int D4_pin = 18;
static constexpr int D5_pin = 17;
static constexpr int D6_pin = 16;
static constexpr int D7_pin = 15;
LiquidCrystal_OLED lcd(Rs_pin, Rw_pin, En_pin, D4_pin, D5_pin, D6_pin, D7_pin);
enum class Icon : uint8_t {
  Status,
  WiFi,
  MusicalNote,
  Saved,
  Shuffle,
  RepeatOne,
  RepeatAll,
  Underbar,
};
uint8_t paused[] = {
  B01010,
  B01010,
  B01010,
  B01010,
  B01010,
  B01010,
  B01010,
  B00000
};
uint8_t playing[] = {
  B10000,
  B11000,
  B11100,
  B11110,
  B11100,
  B11000,
  B10000,
  B00000
};
uint8_t stopped[] = {
  B11110,
  B11110,
  B11110,
  B11110,
  B11110,
  B11110,
  B11110,
  B00000
};
uint8_t singleQuote[] = {
  B00100,
  B00100,
  B00100,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000
};
uint8_t wifi[] = {
  B11111,
  B00000,
  B11110,
  B00000,
  B11100,
  B00000,
  B11000,
  B00000
};
uint8_t musicalNote[] = {
  B00100,
  B00110,
  B00101,
  B00101,
  B11100,
  B11100,
  B11100,
  B00000
};
uint8_t musicalNote2[] = {
  B01000,
  B01100,
  B01011,
  B01000,
  B11011,
  B11000,
  B00011,
  B00000
};
uint8_t saved[] = {
  B00100,
  B00100,
  B11111,
  B01110,
  B00100,
  B11111,
  B11111,
  B00000
};
uint8_t shuffle[] = {
  B10010,
  B10111,
  B10010,
  B01100,
  B10010,
  B10111,
  B10010,
  B00000
};
uint8_t repeatAll[] = {
  B01111,
  B10001,
  B10010,
  B10101,
  B10111,
  B10101,
  B01111,
  B00000
};
uint8_t repeatOne[] = {
  B01111,
  B10001,
  B10110,
  B10010,
  B10010,
  B10111,
  B01111,
  B00000
};
uint8_t underbar[] = {
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B11111,
  B00000
};

#include <ESP_I2S.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include "secrets.h"
#include "freertos/stream_buffer.h"
#include "usb/usb_host.h"

// Step 9: treat the audio area as one continuous stream. TOC entries are used
// only for seeking and for announcing track-number changes during playback.

static constexpr int I2S_SDIN_PIN = 4;   // PCM5102Aの場合　DIN
static constexpr int I2S_BCLK_PIN = 5;   // PCM5102Aの場合　BCK
static constexpr int I2S_LRCLK_PIN = 6;  // PCM5102Aの場合　LCK
static constexpr int I2S_MCLK_PIN = 7;   // PCM5102Aの場合　未接続

static constexpr int BUTTON_UP_PIN = 10;
static constexpr int BUTTON_DOWN_PIN = 11;
static constexpr int BUTTON_ACTION_PIN = 12;
static constexpr int BUTTON_MODE_PIN = 13;
static constexpr size_t CDDA_SECTOR_BYTES = 2352;
// About 533 ms of protection against seek/retry delays caused by vibration.
static constexpr size_t AUDIO_PREBUFFER_SECTORS = 40;
static constexpr size_t AUDIO_BUFFER_SECTORS = 48;

static I2SClass i2s;
//static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
//    U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
static StreamBufferHandle_t audioBuffer = nullptr;
static TaskHandle_t audioTaskHandle = nullptr;
static volatile uint32_t audioBytesWritten = 0;
static volatile uint32_t audioUnderrunChunks = 0;
static volatile bool playbackPaused = false;
static volatile bool playbackFinished = false;
static volatile bool readCdFinished = false;
static volatile bool playbackAbort = false;
static volatile bool userStopRequested = false;
static volatile uint8_t volumePercent = 50;
static volatile bool volumeMuted = false;
static volatile bool readPausedWaiting = false;
static volatile bool pauseKeepAlive = false;
static bool stoppedMediaPoll = false;

static usb_host_client_handle_t usbClient = nullptr;
static usb_device_handle_t usbDevice = nullptr;
static usb_transfer_t *botTransfer = nullptr;
static uint8_t mscInterface = 0xFF;
static uint8_t bulkInEndpoint = 0;
static uint8_t bulkOutEndpoint = 0;
static uint32_t botTag = 0;
static uint32_t scsiDataLength = 0;
static uint8_t lastSenseKey = 0;
static uint8_t lastSenseAsc = 0;
static uint8_t lastSenseAscq = 0;
static uint8_t unitAttentionRetries = 0;
static uint8_t readyPollCount = 0;
static uint8_t cswStallRetries = 0;
static bool scsiCommandScheduled = false;
static uint32_t scsiCommandDueAt = 0;
struct TocTrack {
  uint8_t number;
  int32_t startLba;
  bool isAudio;
};
static TocTrack tocTracks[99] = {};
static uint8_t tocTrackCount = 0;
static int32_t discLeadOutLba = 0;
static bool discLeadOutFound = false;
static volatile uint32_t discGeneration = 0;
static volatile bool cddbLookupRequested = false;
static volatile bool cddbLookupRunning = false;
static String cddbDiscTitle;
static String cddbTrackTitles[99];
static String cddbCategory;
static String cddbDiscIdText;
static volatile bool cddbMetadataReady = false;
static constexpr uint8_t CDDB_MAX_CANDIDATES = 12;
struct CddbCandidate {
  String category;
  String discId;
  String title;
  String artist;      //追加
  String titles[99];  //追加
};
static String mbdbDiscId = "";                   //追加
static bool mbdbDiscIdSaved = false;             //追加
static bool mbdbDiscIdSavedShowMessage = false;  //追加
static unsigned long mbdbDiscIdSavedPrevMillis;  //追加
static bool deleteDiskInfoProcessing = false;    //追加
static String tmpMetadata = "";                  //追加
static bool cddbParseMatchLine(const String &line, CddbCandidate &candidate);
static uint8_t cddbParseMatches(const String &body, CddbCandidate matches[CDDB_MAX_CANDIDATES]);
static CddbCandidate cddbCandidates[CDDB_MAX_CANDIDATES];
static volatile uint8_t cddbCandidateCount = 0;
static volatile uint8_t cddbCandidateSelection = 0;
static volatile bool cddbCandidatesReady = false;
static volatile int8_t cddbReadCandidateRequested = -1;
enum class CddbStatus : uint8_t {
  Idle,
  WaitingWifi,
  Querying,
  Reading,
  Ready,
  NoMatch,
  NetworkError
};
static volatile CddbStatus cddbStatus = CddbStatus::Idle;
static bool wifiWasConnected = false;
static bool wifiBeginIssued = false;
static bool wifiScanStarted = false;
static bool wifiScanFinished = false;
static wifi_auth_mode_t configuredWifiAuth = WIFI_AUTH_MAX;
static int32_t configuredWifiChannel = 0;
static uint8_t configuredWifiBssid[6] = {};
static bool configuredWifiBssidFound = false;
static uint8_t displayedTrackIndex = 0;
static volatile int8_t requestedTrackIndex = -1;
static volatile int32_t requestedPlaybackLba = -1;
static volatile int32_t playbackPlayedLba = 0;
static int32_t playbackStartLba = 0;
static volatile uint8_t playbackTrackIndex = 0;
static uint16_t tocTransferLength = 4;
static uint32_t playbackSectorTarget = 0;
static int32_t readCdLba = 0;
static uint32_t readCdSectorsCompleted = 0;
static uint32_t readCdShortSectors = 0;
static uint32_t readCdBytesReceived = 0;
static uint32_t readCdStartedAt = 0;
static uint32_t readCdHash = 2166136261UL;
static uint32_t concealedSectorCount = 0;
static uint8_t silenceSector[CDDA_SECTOR_BYTES] = {};

enum class UiMode : uint8_t {
  Track,
  Volume,
  PlayMode,
  Cddb
};
static UiMode uiMode = UiMode::Track;

enum class RepeatMode : uint8_t {
  Off,
  All,
  One
};
static volatile RepeatMode repeatMode = RepeatMode::Off;
static volatile bool shuffleEnabled = false;
static volatile int8_t autoStartTrackIndex = -1;
static uint8_t shuffleOrder[99] = {};
static uint8_t shuffleOrderCount = 0;
static uint8_t shuffleOrderPosition = 0;

enum class PlayerStatus : uint8_t {
  WaitingDrive,
  CheckingDisc,
  WaitingDisc,
  SpinningUp,
  ReadingToc,
  Buffering,
  Playing,
  Paused,
  Stopped,
  Error
};
static volatile PlayerStatus playerStatus = PlayerStatus::WaitingDrive;
static volatile PlayerStatus prevPlayerStatus = PlayerStatus::Playing;  // 追加

static void buildShuffleOrder(int firstTrackIndex = -1);
static void requestCddbLookup();

static void buildShuffleOrder(int firstTrackIndex) {
  shuffleOrderCount = 0;
  shuffleOrderPosition = 0;
  for (uint8_t i = 0; i < tocTrackCount; ++i) {
    if (tocTracks[i].isAudio) shuffleOrder[shuffleOrderCount++] = i;
  }
  for (int i = shuffleOrderCount - 1; i > 0; --i) {
    const int j = random(i + 1);
    const uint8_t tmp = shuffleOrder[i];
    shuffleOrder[i] = shuffleOrder[j];
    shuffleOrder[j] = tmp;
  }
  if (firstTrackIndex >= 0) {
    for (uint8_t i = 0; i < shuffleOrderCount; ++i) {
      if (shuffleOrder[i] == firstTrackIndex) {
        const uint8_t tmp = shuffleOrder[0];
        shuffleOrder[0] = shuffleOrder[i];
        shuffleOrder[i] = tmp;
        break;
      }
    }
  }
  Serial.print("Shuffle order:");
  for (uint8_t i = 0; i < shuffleOrderCount; ++i) {
    Serial.printf(" %u", tocTracks[shuffleOrder[i]].number);
  }
  Serial.println();
}

enum class BotPhase : uint8_t {
  Idle,
  Cbw,
  DataIn,
  Csw,
};
static BotPhase botPhase = BotPhase::Idle;
static bool setSpeed = false;  //追加

enum class ScsiCommand : uint8_t {
  Inquiry,
  TestUnitReady,
  RequestSense,
  ReadTocHeader,
  ReadToc,
  ReadCd,
  SetCdSpeed2x  // 追加
};
static ScsiCommand scsiCommand = ScsiCommand::Inquiry;
static ScsiCommand commandBeforeSense = ScsiCommand::TestUnitReady;
static ScsiCommand scheduledScsiCommand = ScsiCommand::TestUnitReady;

static void audioOutputTask(void *) {
  uint8_t chunk[1024];  // Divisible by one 4-byte stereo sample frame.
  const size_t prebufferBytes = AUDIO_PREBUFFER_SECTORS * CDDA_SECTOR_BYTES;

  while (true) {
    while (xStreamBufferBytesAvailable(audioBuffer) < prebufferBytes) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (playbackAbort) {
      vTaskDelay(pdMS_TO_TICKS(5));
      xStreamBufferReset(audioBuffer);
      continue;
    }
    Serial.println("I2S playback started (44.1 kHz, 16-bit, stereo).");
    playbackFinished = false;
    playerStatus = playbackPaused ? PlayerStatus::Paused
                                  : PlayerStatus::Playing;
    displayedTrackIndex = 0;
    while (displayedTrackIndex + 1 < tocTrackCount && playbackStartLba >= tocTracks[displayedTrackIndex + 1].startLba) {
      ++displayedTrackIndex;
    }
    if (tocTrackCount > 0) {
      Serial.printf("Track %u\n", tocTracks[0].number);
    }

    while (!playbackAbort && (uint64_t)audioBytesWritten < (uint64_t)playbackSectorTarget * CDDA_SECTOR_BYTES) {
      memset(chunk, 0, sizeof(chunk));
      if (playbackPaused) {
        playerStatus = PlayerStatus::Paused;
        i2s.write(chunk, sizeof(chunk));
        continue;
      }
      playerStatus = PlayerStatus::Playing;
      const uint64_t bytesRemaining =
        (uint64_t)playbackSectorTarget * CDDA_SECTOR_BYTES - audioBytesWritten;
      const size_t wanted = min((uint64_t)sizeof(chunk), bytesRemaining);
      const size_t received = xStreamBufferReceive(
        audioBuffer, chunk, wanted, pdMS_TO_TICKS(2));
      if (received < wanted) {
        // Keep BCLK/LRCLK/MCLK and DMA running. Missing samples become silence
        // instead of a stopped/underrunning I2S stream that sounds like noise.
        ++audioUnderrunChunks;
      }
      const int32_t gainQ15 =
        volumeMuted ? 0 : volumePercent * 32768L / 100;
      for (size_t i = 0; i + 1 < received; i += 2) {
        const int16_t sample =
          (int16_t)(((uint16_t)chunk[i + 1] << 8) | chunk[i]);
        const int16_t scaled =
          (int16_t)(((int32_t)sample * gainQ15) >> 15);
        chunk[i] = (uint16_t)scaled & 0xFF;
        chunk[i + 1] = ((uint16_t)scaled >> 8) & 0xFF;
      }
      const size_t written = i2s.write(chunk, sizeof(chunk));
      audioBytesWritten += received;
      if (written != sizeof(chunk)) {
        Serial.printf("I2S short write: %u/%u bytes\n", (unsigned)written,
                      (unsigned)sizeof(chunk));
      }

      const int32_t playedLba =
        playbackStartLba + audioBytesWritten / CDDA_SECTOR_BYTES;
      playbackPlayedLba = playedLba;
      const bool reachedSingleTrackEnd =
        (shuffleEnabled || repeatMode == RepeatMode::One) && (uint64_t)audioBytesWritten >= (uint64_t)playbackSectorTarget * CDDA_SECTOR_BYTES;
      while (!reachedSingleTrackEnd && displayedTrackIndex + 1 < tocTrackCount && playedLba >= tocTracks[displayedTrackIndex + 1].startLba) {
        ++displayedTrackIndex;
        Serial.printf("Track %u\n", tocTracks[displayedTrackIndex].number);
      }
    }
    if (playbackAbort) {
      vTaskDelay(pdMS_TO_TICKS(5));
      memset(chunk, 0, sizeof(chunk));
      i2s.write(chunk, sizeof(chunk));
      if (!userStopRequested) playbackFinished = false;
      continue;
    }
    Serial.println("[DEBUG] playbackFinished");
    Serial.printf("I2S playback finished: %lu bytes written.\n",
                  (unsigned long)audioBytesWritten);
    Serial.printf("I2S underrun/silence chunks: %lu.\n",
                  (unsigned long)audioUnderrunChunks);
    playbackFinished = true;
    playerStatus = PlayerStatus::Stopped;
    stoppedMediaPoll = true;
    int8_t nextTrack = -1;
    if (!userStopRequested && tocTrackCount > 0) {
      if (repeatMode == RepeatMode::One) {
        nextTrack = playbackTrackIndex;
      } else if (shuffleEnabled) {
        if (shuffleOrderPosition + 1 < shuffleOrderCount) {
          nextTrack = shuffleOrder[++shuffleOrderPosition];
        } else if (repeatMode == RepeatMode::All) {
          buildShuffleOrder();
          if (shuffleOrderCount > 0) nextTrack = shuffleOrder[0];
        }
      } else if (repeatMode == RepeatMode::All) {
        nextTrack = 0;
      }
    }
    if (nextTrack >= 0) {
      autoStartTrackIndex = nextTrack;
      playerStatus = PlayerStatus::Buffering;
    } else if (tocTrackCount > 0) {
      displayedTrackIndex = 0;
      playbackStartLba = tocTracks[0].startLba;
      playbackPlayedLba = tocTracks[0].startLba;
      Serial.println("Playback stopped. Returned to Track 1.");
    }
    // Return to the prebuffer wait at the top of the loop. A button-triggered
    // seek can now fill the buffer and start playback again.
  }
}

static bool startI2sAudio() {
  audioBuffer = xStreamBufferCreate(
    AUDIO_BUFFER_SECTORS * CDDA_SECTOR_BYTES + 1, 1);
  if (audioBuffer == nullptr) {
    Serial.println("Could not allocate CD-DA audio ring buffer.");
    return false;
  }

  i2s.setPins(I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_SDIN_PIN, -1, I2S_MCLK_PIN);
  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("Could not initialize I2S output.");
    vStreamBufferDelete(audioBuffer);
    audioBuffer = nullptr;
    return false;
  }

  if (xTaskCreate(audioOutputTask, "cdda_i2s", 4096, nullptr, 2, &audioTaskHandle) != pdPASS) {
    Serial.println("Could not create I2S output task.");
    i2s.end();
    vStreamBufferDelete(audioBuffer);
    audioBuffer = nullptr;
    return false;
  }
  Serial.printf("I2S ready: SDIN=%d BCLK=%d LRCLK=%d MCLK=%d, volume=%u%%.\n",
                I2S_SDIN_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_MCLK_PIN, volumePercent);
  return true;
}

struct __attribute__((packed)) CommandBlockWrapper {
  uint32_t signature;
  uint32_t tag;
  uint32_t transferLength;
  uint8_t flags;
  uint8_t lun;
  uint8_t commandLength;
  uint8_t command[16];
};

struct __attribute__((packed)) CommandStatusWrapper {
  uint32_t signature;
  uint32_t tag;
  uint32_t residue;
  uint8_t status;
};

static_assert(sizeof(CommandBlockWrapper) == 31, "Invalid BOT CBW size");
static_assert(sizeof(CommandStatusWrapper) == 13, "Invalid BOT CSW size");

static void handleBotTransfer(usb_transfer_t *transfer);
static void startScsiCommand(ScsiCommand command);
static void scheduleScsiCommand(ScsiCommand command, uint32_t delayMs);
static bool submitBotTransfer(uint8_t endpoint, int bytes, BotPhase phase);
static const char *scsiCommandName(ScsiCommand command);

static void scheduleScsiCommand(ScsiCommand command, uint32_t delayMs) {
  scheduledScsiCommand = command;
  scsiCommandDueAt = millis() + delayMs;
  scsiCommandScheduled = true;
}

static bool submitBotTransfer(uint8_t endpoint, int bytes, BotPhase phase) {
  botTransfer->device_handle = usbDevice;
  botTransfer->bEndpointAddress = endpoint;
  botTransfer->num_bytes = bytes;
  botTransfer->callback = handleBotTransfer;
  botTransfer->context = nullptr;
  botPhase = phase;
  esp_err_t err = usb_host_transfer_submit(botTransfer);
  if (err != ESP_OK) {
    botPhase = BotPhase::Idle;
    Serial.printf("BOT transfer submit failed: %s\n", esp_err_to_name(err));
    return false;
  }
  return true;
}

static void printInquiryResponse(const uint8_t *data, size_t length) {
  if (length < 36) {
    Serial.printf("Short INQUIRY response: %u byte(s)\n", (unsigned)length);
    return;
  }

  char vendor[9] = {};
  char product[17] = {};
  char revision[5] = {};
  memcpy(vendor, data + 8, 8);
  memcpy(product, data + 16, 16);
  memcpy(revision, data + 32, 4);

  const uint8_t peripheralType = data[0] & 0x1F;
  Serial.println("SCSI INQUIRY response:");
  Serial.printf("  Peripheral type: 0x%02X%s\n", peripheralType, peripheralType == 0x05 ? " (CD/DVD device)" : "");
  Serial.printf("  Removable media: %s\n", (data[1] & 0x80) ? "yes" : "no");
  Serial.printf("  Vendor:   [%s]\n", vendor);
  Serial.printf("  Product:  [%s]\n", product);
  Serial.printf("  Revision: [%s]\n", revision);
}

static const char *senseDescription(uint8_t key, uint8_t asc, uint8_t ascq) {
  if (key == 0x02 && asc == 0x3A) return "No medium in drive";
  if (key == 0x02 && asc == 0x04 && ascq == 0x01) return "Becoming ready";
  if (key == 0x02 && asc == 0x04) return "Logical unit not ready";
  if (key == 0x06 && asc == 0x28) return "Medium may have changed";
  if (key == 0x06 && asc == 0x29) return "Device reset occurred";
  if (key == 0x05 && asc == 0x20) return "Unsupported command";
  return "See SCSI sense key / ASC / ASCQ";
}

static void enterNoDiscState() {
  ++discGeneration;
  cddbLookupRequested = false;
  cddbMetadataReady = false;
  cddbCandidatesReady = false;
  cddbCandidateCount = 0;
  cddbReadCandidateRequested = -1;
  cddbStatus = CddbStatus::Idle;
  stoppedMediaPoll = false;
  cddbDiscTitle = "";
  cddbCategory = "";
  cddbDiscIdText = "";
  for (String &title : cddbTrackTitles) title = "";
  playbackAbort = true;
  userStopRequested = false;
  playbackPaused = false;
  pauseKeepAlive = false;
  readPausedWaiting = false;
  readCdFinished = false;
  playbackFinished = false;
  requestedTrackIndex = -1;
  requestedPlaybackLba = -1;
  tocTrackCount = 0;
  discLeadOutFound = false;
  shuffleOrderCount = 0;
  shuffleOrderPosition = 0;
  playerStatus = PlayerStatus::WaitingDisc;
  Serial.println("No disc. Waiting for media insertion...");
}

static void printRequestSenseResponse(const uint8_t *data, size_t length) {
  if (length < 14) {
    Serial.printf("Short REQUEST SENSE response: %u byte(s)\n", (unsigned)length);
    return;
  }
  const uint8_t key = data[2] & 0x0F;
  const uint8_t asc = data[12];
  const uint8_t ascq = data[13];
  lastSenseKey = key;
  lastSenseAsc = asc;
  lastSenseAscq = ascq;
  Serial.println("SCSI REQUEST SENSE response:");
  Serial.printf("  Sense Key: 0x%02X\n", key);
  Serial.printf("  ASC/ASCQ:  0x%02X/0x%02X\n", asc, ascq);
  Serial.printf("  Meaning:   %s\n", senseDescription(key, asc, ascq));
}

static void printTocResponse(const uint8_t *data, size_t length) {
  if (length < 4) {
    Serial.printf("Short READ TOC response: %u byte(s)\n", (unsigned)length);
    return;
  }

  const size_t reportedLength = ((size_t)data[0] << 8) | data[1];
  const size_t totalLength = min(length, reportedLength + 2);
  tocTrackCount = 0;
  discLeadOutFound = false;
  Serial.println("CD Table of Contents:");
  Serial.printf("  First track: %u\n", data[2]);
  Serial.printf("  Last track:  %u\n", data[3]);

  for (size_t offset = 4; offset + 8 <= totalLength; offset += 8) {
    const uint8_t *entry = data + offset;
    const uint8_t control = entry[1] & 0x0F;
    const uint8_t track = entry[2];
    const uint8_t minute = entry[5];
    const uint8_t second = entry[6];
    const uint8_t frame = entry[7];
    const char *kind = (control & 0x04) ? "data" : "audio";
    const int32_t entryLba = ((int32_t)minute * 60 + second) * 75 + frame - 150;

    if (track == 0xAA) {
      discLeadOutLba = entryLba;
      discLeadOutFound = true;
      Serial.printf("  Lead-out: %02u:%02u:%02u\n", minute, second, frame);
    } else {
      Serial.printf("  Track %2u: %02u:%02u:%02u  %s\n", track, minute, second, frame, kind);
      if (tocTrackCount < 99) {
        tocTracks[tocTrackCount].number = track;
        tocTracks[tocTrackCount].startLba = entryLba;
        tocTracks[tocTrackCount].isAudio = !(control & 0x04);
        ++tocTrackCount;
      }
    }
  }
}

static bool parseTocHeader(const uint8_t *data, size_t length) {
  if (length < 4 || data[3] < data[2]) {
    Serial.println("Invalid READ TOC header.");
    return false;
  }
  const uint16_t trackDescriptors = (uint16_t)data[3] - data[2] + 1;
  // Four header bytes, one descriptor per track, and one lead-out descriptor.
  tocTransferLength = 4 + (trackDescriptors + 1) * 8;
  Serial.printf("TOC header: tracks %u-%u, requesting %u bytes.\n", data[2], data[3], tocTransferLength);
  return true;
}

static void cddbLookupTask(void *) {
  // musicbrainz.orgから取得
  musicBrainzLookup();
  vTaskDelete(nullptr);
}

static void requestCddbLookup() {
  cddbLookupRequested = true;
  cddbStatus = WiFi.status() == WL_CONNECTED ? CddbStatus::Querying : CddbStatus::WaitingWifi;
}

static const char *wifiAuthName(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "OTHER";
  }
}

static void handleWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.println("Wi-Fi associated with access point; waiting for DHCP...");
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.print("Wi-Fi DHCP completed, IP: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("Wi-Fi connection failed/disconnected (reason %u", reason);
    switch (reason) {
      case WIFI_REASON_NO_AP_FOUND: Serial.print(": AP not found"); break;
      case WIFI_REASON_AUTH_FAIL: Serial.print(": authentication failed"); break;
      case WIFI_REASON_AUTH_EXPIRE: Serial.print(": authentication expired"); break;
      case WIFI_REASON_HANDSHAKE_TIMEOUT:
        Serial.print(": WPA handshake timeout");
        break;
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        Serial.print(": WPA 4-way handshake timeout");
        break;
      case WIFI_REASON_ASSOC_FAIL: Serial.print(": association failed"); break;
      default: break;
    }
    Serial.println(").");
  }
}

static void serviceWifiAndCddb() {
  const bool configured = strlen(WIFI_SSID) != 0;
  if (!configured) return;

  if (!wifiScanFinished) {
    if (!wifiScanStarted) {
      wifiScanStarted = true;
      Serial.println("Scanning nearby Wi-Fi access points...");
      WiFi.scanNetworks(true, true);
      return;
    }

    const int16_t count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    wifiScanFinished = true;
    if (count == WIFI_SCAN_FAILED) {
      Serial.println("Wi-Fi scan failed; continuing with connection.");
    } else {
      Serial.printf("Wi-Fi scan complete: %d AP(s) found.\n", count);
      for (int16_t i = 0; i < count; ++i) {
        if (WiFi.SSID(i) == WIFI_SSID) {
          configuredWifiAuth = WiFi.encryptionType(i);
          configuredWifiChannel = WiFi.channel(i);
          const uint8_t *bssid = WiFi.BSSID(i);
          if (bssid != nullptr) {
            memcpy(configuredWifiBssid, bssid, sizeof(configuredWifiBssid));
            configuredWifiBssidFound = true;
          }
        }
        Serial.printf("  %2d: RSSI %4d dBm  CH %2d  %-9s  %s%s\n", i + 1,
                      WiFi.RSSI(i), WiFi.channel(i),
                      wifiAuthName(WiFi.encryptionType(i)),
                      WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>",
                      WiFi.SSID(i) == WIFI_SSID ? "  <-- configured" : "");
      }
    }
    WiFi.scanDelete();
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      Serial.println("Wi-Fi disconnected.");
    }
    if (!wifiBeginIssued) {
      wifiBeginIssued = true;
      Serial.printf("Connecting Wi-Fi: %s (password length %u)\n", WIFI_SSID,
                    (unsigned)strlen(WIFI_PASSWORD));
      Serial.printf("ESP32 station MAC: %s\n", WiFi.macAddress().c_str());
      WiFi.setAutoReconnect(true);
      WiFi.setSleep(false);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      Serial.printf("Wi-Fi TX power set to %.1f dBm.\n", (int)WiFi.getTxPower() / 4.0f);
      // Only lower the security floor when the configured AP is actually WEP.
      // Keeping WPA2 as the floor for WPA2/WPA3 networks avoids mixing the
      // legacy compatibility setting into a normal WPA connection.
      WiFi.setMinSecurity(configuredWifiAuth == WIFI_AUTH_WEP ? WIFI_AUTH_WEP : WIFI_AUTH_WPA2_PSK);
      if (configuredWifiBssidFound) {
        Serial.printf("Locking connection to channel %ld, BSSID %02X:%02X:%02X:%02X:%02X:%02X\n",
                      (long)configuredWifiChannel,
                      configuredWifiBssid[0], configuredWifiBssid[1], configuredWifiBssid[2],
                      configuredWifiBssid[3], configuredWifiBssid[4], configuredWifiBssid[5]);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, configuredWifiChannel, configuredWifiBssid);
      } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
    }
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
  }
  if (cddbLookupRequested && !cddbLookupRunning && tocTrackCount > 0 && discLeadOutFound) {
    cddbLookupRequested = false;
    cddbLookupRunning = true;
    if (xTaskCreate(cddbLookupTask, "cddb", 12288, nullptr, 1, nullptr) != pdPASS) {
      cddbLookupRunning = false;
      cddbLookupRequested = true;
      Serial.println("CDDB: could not start lookup task.");
    }
  }
}

static void printCdAudioSector(const uint8_t *data, size_t length) {
  Serial.printf("CD-DA sector received: %u byte(s)\n", (unsigned)length);
  if (length < 2352) {
    Serial.println("RESULT: CD-DA sector was shorter than 2352 bytes.");
    return;
  }

  size_t nonZeroBytes = 0;
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < 2352; ++i) {
    if (data[i] != 0) ++nonZeroBytes;
    hash = (hash ^ data[i]) * 16777619UL;
  }
  Serial.printf("  Non-zero bytes: %u / 2352\n", (unsigned)nonZeroBytes);
  Serial.printf("  FNV-1a checksum: %08lX\n", (unsigned long)hash);
  Serial.println("  First 8 stereo sample frames (signed 16-bit, little-endian):");
  for (size_t i = 0; i < 8; ++i) {
    const size_t p = i * 4;
    const int16_t left = (int16_t)(((uint16_t)data[p + 1] << 8) | data[p]);
    const int16_t right = (int16_t)(((uint16_t)data[p + 3] << 8) | data[p + 2]);
    Serial.printf("    %u: L=%6d R=%6d\n", (unsigned)i, left, right);
  }
  Serial.println("RESULT: One raw CD-DA audio sector reached ESP32 RAM.");
}

static void accountCdAudioSector(const uint8_t *data, size_t length) {
  readCdBytesReceived += length;
  if (length != 2352) ++readCdShortSectors;
  for (size_t i = 0; i < length; ++i) {
    readCdHash = (readCdHash ^ data[i]) * 16777619UL;
  }
}

static void queueCdAudioSector(uint8_t *data, size_t length) {
  if (audioBuffer == nullptr || length != CDDA_SECTOR_BYTES) return;

  // MMC READ CD returns each CD-DA sample least-significant byte first:
  // left low, left high, right low, right high. That already matches ESP32's
  // little-endian 16-bit I2S buffer layout, so no byte swapping is needed.

  const size_t queued = xStreamBufferSend(
    audioBuffer, data, length, pdMS_TO_TICKS(100));
  if (queued != length) {
    Serial.printf("Audio buffer overflow: queued %u/%u bytes\n", (unsigned)queued, (unsigned)length);
  }
}

static void concealFailedReadCdSector() {
  ++concealedSectorCount;
  Serial.printf("READ CD LBA %ld failed; inserting silence (concealed sectors: %lu).\n",
                (long)readCdLba, (unsigned long)concealedSectorCount);
  accountCdAudioSector(silenceSector, sizeof(silenceSector));
  queueCdAudioSector(silenceSector, sizeof(silenceSector));
  ++readCdSectorsCompleted;

  if (readCdSectorsCompleted >= playbackSectorTarget) {
    readCdFinished = true;
    printReadCdBenchmark();
    return;
  }

  ++readCdLba;
  if (playbackPaused) {
    readPausedWaiting = true;
    pauseKeepAlive = true;
    scheduleScsiCommand(ScsiCommand::ReadCd, 500);
  } else {
    startScsiCommand(ScsiCommand::ReadCd);
  }
}

static void printReadCdBenchmark() {
  uint32_t elapsedMs = millis() - readCdStartedAt;
  if (elapsedMs == 0) elapsedMs = 1;
  const uint32_t bytesPerSecond = ((uint64_t)readCdBytesReceived * 1000) / elapsedMs;
  const uint32_t sectorsPerSecondX100 = ((uint64_t)readCdSectorsCompleted * 100000) / elapsedMs;

  Serial.println("CD-DA continuous read benchmark:");
  Serial.printf("  Sectors:       %lu / %lu\n",
                (unsigned long)readCdSectorsCompleted,
                (unsigned long)playbackSectorTarget);
  Serial.printf("  Bytes:         %lu\n", (unsigned long)readCdBytesReceived);
  Serial.printf("  Elapsed:       %lu ms\n", (unsigned long)elapsedMs);
  Serial.printf("  Throughput:    %lu bytes/s\n", (unsigned long)bytesPerSecond);
  Serial.printf("  Sector rate:   %lu.%02lu sectors/s\n",
                (unsigned long)(sectorsPerSecondX100 / 100),
                (unsigned long)(sectorsPerSecondX100 % 100));
  Serial.printf("  Short sectors: %lu\n",
                (unsigned long)readCdShortSectors);
  Serial.printf("  Concealed:     %lu sector(s)\n",
                (unsigned long)concealedSectorCount);
  Serial.printf("  Stream hash:   %08lX\n", (unsigned long)readCdHash);
  Serial.printf("RESULT: Continuous read is %s for real-time CD audio.\n",
                bytesPerSecond >= 176400 && readCdShortSectors == 0
                  ? "fast enough"
                  : "NOT fast enough");
}

static const char *scsiCommandName(ScsiCommand command) {
  switch (command) {
    case ScsiCommand::Inquiry: return "INQUIRY";
    case ScsiCommand::TestUnitReady: return "TEST UNIT READY";
    case ScsiCommand::RequestSense: return "REQUEST SENSE";
    case ScsiCommand::ReadTocHeader: return "READ TOC header";
    case ScsiCommand::ReadToc: return "READ TOC/PMA/ATIP";
    case ScsiCommand::ReadCd: return "READ CD";
    case ScsiCommand::SetCdSpeed2x: return "SET CD SPEED";
  }
  return "unknown";
}

static void handleBotTransfer(usb_transfer_t *transfer) {
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
    if (transfer->status == USB_TRANSFER_STATUS_STALL && botPhase == BotPhase::Csw && cswStallRetries < 1) {
      ++cswStallRetries;
      Serial.println("CSW Bulk IN stalled; clearing endpoint halt and retrying...");
      esp_err_t clearErr = usb_host_endpoint_clear(usbDevice, bulkInEndpoint);
      if (clearErr == ESP_OK) {
        memset(transfer->data_buffer, 0, 64);
        submitBotTransfer(bulkInEndpoint, 64, BotPhase::Csw);
        return;
      }
      Serial.printf("Could not clear Bulk IN halt: %s\n", esp_err_to_name(clearErr));
    }
    Serial.printf("BOT phase %u failed, USB status %u\n", (unsigned)botPhase, (unsigned)transfer->status);
    botPhase = BotPhase::Idle;
    return;
  }

  if (botPhase == BotPhase::Cbw) {
    memset(transfer->data_buffer, 0, 64);
    // Bulk IN requests must be a multiple of the endpoint's 64-byte MPS.
    // Commands without a data phase proceed directly to their CSW.
    const int receiveBytes = (scsiDataLength + 63) & ~63;
    submitBotTransfer(bulkInEndpoint, receiveBytes == 0 ? 64 : receiveBytes,
                      scsiDataLength == 0 ? BotPhase::Csw : BotPhase::DataIn);
  } else if (botPhase == BotPhase::DataIn) {
    // On a failed data-in command, some BOT devices return the 13-byte CSW
    // immediately on the Bulk IN endpoint instead of a data payload. Consume
    // that CSW here; otherwise a second IN request would stall the endpoint.
    if (transfer->actual_num_bytes == (int)sizeof(CommandStatusWrapper)) {
      CommandStatusWrapper earlyCsw = {};
      memcpy(&earlyCsw, transfer->data_buffer, sizeof(earlyCsw));
      if (earlyCsw.signature == 0x53425355) {
        botPhase = BotPhase::Csw;
        handleBotTransfer(transfer);
        return;
      }
    }

    if (scsiCommand == ScsiCommand::Inquiry) {
      printInquiryResponse(transfer->data_buffer, transfer->actual_num_bytes);
    } else if (scsiCommand == ScsiCommand::RequestSense) {
      printRequestSenseResponse(transfer->data_buffer, transfer->actual_num_bytes);
    } else if (scsiCommand == ScsiCommand::ReadTocHeader) {
      parseTocHeader(transfer->data_buffer, transfer->actual_num_bytes);
    } else if (scsiCommand == ScsiCommand::ReadToc) {
      printTocResponse(transfer->data_buffer, transfer->actual_num_bytes);
    } else if (scsiCommand == ScsiCommand::ReadCd) {
      if (!pauseKeepAlive) {
        accountCdAudioSector(transfer->data_buffer, transfer->actual_num_bytes);
        queueCdAudioSector(transfer->data_buffer, transfer->actual_num_bytes);
      }
    }
    memset(transfer->data_buffer, 0, 64);
    submitBotTransfer(bulkInEndpoint, 64, BotPhase::Csw);
  } else if (botPhase == BotPhase::Csw) {
    if (transfer->actual_num_bytes < (int)sizeof(CommandStatusWrapper)) {
      Serial.printf("Short BOT status response: %d byte(s)\n", transfer->actual_num_bytes);
      botPhase = BotPhase::Idle;
      return;
    }

    CommandStatusWrapper csw = {};
    memcpy(&csw, transfer->data_buffer, sizeof(csw));
    if (csw.signature != 0x53425355 || csw.tag != botTag) {
      Serial.printf("Invalid BOT status: signature %08lX, tag %lu\n",
                    (unsigned long)csw.signature, (unsigned long)csw.tag);
      botPhase = BotPhase::Idle;
    } else {
      if (scsiCommand != ScsiCommand::ReadCd || csw.status != 0 || readCdSectorsCompleted == 0 || readCdSectorsCompleted + 1 == playbackSectorTarget) {
        Serial.printf("SCSI %s %s (status %u, residue %lu)\n", scsiCommandName(scsiCommand),
                      csw.status == 0 ? "completed" : "failed", csw.status, (unsigned long)csw.residue);
      }

      const ScsiCommand completedCommand = scsiCommand;
      const bool commandPassed = csw.status == 0;
      botPhase = BotPhase::Idle;
      if (completedCommand == ScsiCommand::SetCdSpeed2x) {
        if (commandPassed) {
          setSpeed = true;
          //Serial.println("[DEBUG] SetCdSpeed2x成功");
        } else {
          setSpeed = false;
          //Serial.println("[DEBUG] SetCdSpeed2x失敗");
        }
      }
      if (completedCommand == ScsiCommand::Inquiry && commandPassed) {
        startScsiCommand(ScsiCommand::TestUnitReady);
      } else if (completedCommand == ScsiCommand::TestUnitReady) {
        if (commandPassed) {
          if (stoppedMediaPoll) {
            scheduleScsiCommand(ScsiCommand::TestUnitReady, 1000);
            return;
          }
          Serial.println("RESULT: Disc is present and the drive is ready.");
          playerStatus = PlayerStatus::ReadingToc;
          unitAttentionRetries = 0;
          readyPollCount = 0;
          startScsiCommand(ScsiCommand::ReadTocHeader);
        } else {
          commandBeforeSense = ScsiCommand::TestUnitReady;
          startScsiCommand(ScsiCommand::RequestSense);
        }
      } else if (completedCommand == ScsiCommand::ReadTocHeader) {
        if (commandPassed && tocTransferLength > 4) {
          startScsiCommand(ScsiCommand::ReadToc);
        } else if (!commandPassed) {
          commandBeforeSense = ScsiCommand::ReadTocHeader;
          startScsiCommand(ScsiCommand::RequestSense);
        }
      } else if (completedCommand == ScsiCommand::ReadToc) {
        if (!commandPassed) {
          commandBeforeSense = ScsiCommand::ReadToc;
          startScsiCommand(ScsiCommand::RequestSense);
        } else if (tocTrackCount > 0 && tocTracks[0].isAudio && discLeadOutFound && discLeadOutLba > tocTracks[0].startLba) {
          Serial.println("[DEBUG] TOC読込完了");
          startScsiCommand(ScsiCommand::SetCdSpeed2x);
          return;
        } else {
          Serial.println("RESULT: Disc does not start with an audio track.");
          playerStatus = PlayerStatus::Error;
        }
      } else if (completedCommand == ScsiCommand::SetCdSpeed2x) {
        ++discGeneration;
        cddbMetadataReady = false;
        cddbCandidatesReady = false;
        cddbCandidateCount = 0;
        cddbReadCandidateRequested = -1;
        cddbStatus = CddbStatus::Idle;
        cddbDiscTitle = "";
        cddbCategory = "";
        cddbDiscIdText = "";
        for (String &title : cddbTrackTitles) title = "";
        requestCddbLookup();
        uint8_t initialTrackIndex = 0;
        if (shuffleEnabled) {
          buildShuffleOrder();
          if (shuffleOrderCount > 0) initialTrackIndex = shuffleOrder[0];
        }
        const int32_t initialEndLba =
          (shuffleEnabled || repeatMode == RepeatMode::One) && initialTrackIndex + 1 < tocTrackCount ? tocTracks[initialTrackIndex + 1].startLba : discLeadOutLba;
        playbackSectorTarget =
          initialEndLba - tocTracks[initialTrackIndex].startLba;
        Serial.printf("Continuous disc playback: LBA %ld to %ld "
                      "(%lu sectors, %lu seconds).\n",
                      (long)tocTracks[initialTrackIndex].startLba,
                      (long)initialEndLba,
                      (unsigned long)playbackSectorTarget,
                      (unsigned long)(playbackSectorTarget / 75));
        readCdLba = tocTracks[initialTrackIndex].startLba;
        playbackTrackIndex = initialTrackIndex;
        playbackStartLba = readCdLba;
        playbackPlayedLba = readCdLba;
        readCdSectorsCompleted = 0;
        readCdShortSectors = 0;
        readCdBytesReceived = 0;
        readCdHash = 2166136261UL;
        concealedSectorCount = 0;
        readCdStartedAt = millis();
        audioBytesWritten = 0;
        audioUnderrunChunks = 0;
        playbackFinished = false;
        readCdFinished = false;
        playbackAbort = false;
        userStopRequested = false;
        playerStatus = PlayerStatus::Buffering;
        if (audioBuffer != nullptr) {
          xStreamBufferReset(audioBuffer);
        }
        startScsiCommand(ScsiCommand::ReadCd);
      } else if (completedCommand == ScsiCommand::ReadCd) {
        if (userStopRequested) {
          readCdFinished = true;
        } else if (!commandPassed) {
          commandBeforeSense = ScsiCommand::ReadCd;
          startScsiCommand(ScsiCommand::RequestSense);
        } else if (pauseKeepAlive) {
          if (playbackPaused) {
            scheduleScsiCommand(ScsiCommand::ReadCd, 500);
          } else {
            pauseKeepAlive = false;
            readPausedWaiting = false;
            startScsiCommand(ScsiCommand::ReadCd);
          }
        } else {
          ++readCdSectorsCompleted;
          if (requestedTrackIndex >= 0 && requestedTrackIndex < tocTrackCount) {
            const uint8_t target = requestedTrackIndex;
            const int32_t targetLba = requestedPlaybackLba >= 0 ? requestedPlaybackLba : tocTracks[target].startLba;
            requestedTrackIndex = -1;
            requestedPlaybackLba = -1;
            readCdLba = targetLba;
            playbackTrackIndex = target;
            playbackStartLba = readCdLba;
            playbackPlayedLba = readCdLba;
            const int32_t targetEndLba =
              (shuffleEnabled || repeatMode == RepeatMode::One) && target + 1 < tocTrackCount ? tocTracks[target + 1].startLba : discLeadOutLba;
            playbackSectorTarget = targetEndLba - readCdLba;
            readCdSectorsCompleted = 0;
            audioBytesWritten = 0;
            displayedTrackIndex = target;
            if (audioBuffer != nullptr) xStreamBufferReset(audioBuffer);
            Serial.printf("Seeking to Track %u (LBA %ld).\n", tocTracks[target].number, (long)readCdLba);
            startScsiCommand(ScsiCommand::ReadCd);
          } else if (playbackPaused && readCdSectorsCompleted < playbackSectorTarget) {
            readPausedWaiting = true;
            // The completed LBA is already in the audio buffer. Keep the
            // drive spinning by repeatedly reading (and discarding) the next
            // LBA while paused.
            ++readCdLba;
            pauseKeepAlive = true;
            scheduleScsiCommand(ScsiCommand::ReadCd, 500);
          } else if (readCdSectorsCompleted < playbackSectorTarget) {
            ++readCdLba;
            if ((readCdSectorsCompleted % 750) == 0) {
              Serial.printf("  Played about %lu/%lu seconds...\n",
                            (unsigned long)(readCdSectorsCompleted / 75), (unsigned long)(playbackSectorTarget / 75));
            }
            startScsiCommand(ScsiCommand::ReadCd);
          } else {
            readCdFinished = true;
            printReadCdBenchmark();
          }
        }
      } else if (completedCommand == ScsiCommand::RequestSense && commandPassed && lastSenseKey == 0x02 && lastSenseAsc == 0x3A) {
        enterNoDiscState();
        scheduleScsiCommand(ScsiCommand::TestUnitReady, 1000);
      } else if (completedCommand == ScsiCommand::RequestSense && commandPassed && commandBeforeSense == ScsiCommand::ReadCd && (lastSenseKey == 0x01 || lastSenseKey == 0x03 || lastSenseKey == 0x04 || lastSenseKey == 0x0B)) {
        Serial.printf("READ CD sense %02X/%02X/%02X at LBA %ld.\n",
                      lastSenseKey, lastSenseAsc, lastSenseAscq, (long)readCdLba);
        concealFailedReadCdSector();
      } else if (completedCommand == ScsiCommand::RequestSense && commandPassed && lastSenseKey == 0x06 && unitAttentionRetries < 3) {
        // UNIT ATTENTION (for example 06/28/00 after media insertion) is
        // cleared by REQUEST SENSE. Retry the command that encountered it.
        ++unitAttentionRetries;
        Serial.printf("Unit attention cleared; scheduling %s retry (%u/3)...\n",
                      scsiCommandName(commandBeforeSense), unitAttentionRetries);
        scheduleScsiCommand(commandBeforeSense, 250);
      } else if (completedCommand == ScsiCommand::RequestSense && commandPassed && lastSenseKey == 0x02 && lastSenseAsc == 0x04 && readyPollCount < 30) {
        // The drive is spinning up or identifying the newly inserted disc.
        // Poll without blocking USB event handling (up to about 30 seconds).
        ++readyPollCount;
        playerStatus = PlayerStatus::SpinningUp;
        Serial.printf("Drive is becoming ready; retrying in 1 second (%u/30)...\n", readyPollCount);
        scheduleScsiCommand(ScsiCommand::TestUnitReady, 1000);
      }
    }
  }
}

static void startScsiCommand(ScsiCommand command) {
  CommandBlockWrapper cbw = {};
  cbw.signature = 0x43425355;
  cbw.tag = ++botTag;
  cswStallRetries = 0;
  scsiCommand = command;

  if (command == ScsiCommand::Inquiry) {
    cbw.commandLength = 6;
    scsiDataLength = 36;
    cbw.transferLength = scsiDataLength;
    cbw.flags = 0x80;
    cbw.command[0] = 0x12;
    cbw.command[4] = scsiDataLength;
  } else if (command == ScsiCommand::TestUnitReady) {
    cbw.commandLength = 6;
    scsiDataLength = 0;
    cbw.command[0] = 0x00;
  } else if (command == ScsiCommand::RequestSense) {
    cbw.commandLength = 6;
    scsiDataLength = 18;
    lastSenseKey = 0;
    lastSenseAsc = 0;
    lastSenseAscq = 0;
    cbw.transferLength = scsiDataLength;
    cbw.flags = 0x80;
    cbw.command[0] = 0x03;
    cbw.command[4] = scsiDataLength;
  } else if (command == ScsiCommand::ReadTocHeader || command == ScsiCommand::ReadToc) {
    cbw.commandLength = 10;
    scsiDataLength = command == ScsiCommand::ReadTocHeader ? 4 : tocTransferLength;
    cbw.transferLength = scsiDataLength;
    cbw.flags = 0x80;
    cbw.command[0] = 0x43;  // READ TOC/PMA/ATIP
    cbw.command[1] = 0x02;  // MSF addresses
    cbw.command[7] = scsiDataLength >> 8;
    cbw.command[8] = scsiDataLength & 0xFF;
  } else if (command == ScsiCommand::SetCdSpeed2x) {
    Serial.println("[DEBUG] SetCdSpeed2x実行");
    cbw.commandLength = 12;  // 12バイトCDB
    scsiDataLength = 0;      // SET CD SPEEDは追加データ転送なし（コマンドのみ）
    cbw.command[0] = 0xBB;   // 0: Opcode: SET CD SPEED
    cbw.command[1] = 0x00;   // 1: Reserved
    cbw.command[2] = 0x01;   // 2-3: Read Speed (0x0160 = 352) -> 2倍速
    cbw.command[3] = 0x60;
    cbw.command[4] = 0x00;  // 4-5: Write Speed 指定なし
    cbw.command[5] = 0x00;
    cbw.command[6] = 0x00;  // 6-7: Reserved
    cbw.command[7] = 0x00;
    cbw.command[8] = 0x00;  // 8-9: Reserved
    cbw.command[9] = 0x00;
    cbw.command[10] = 0x00;  // 10-11: Control / Reserved
    cbw.command[11] = 0x00;
  } else {
    cbw.commandLength = 12;
    scsiDataLength = 2352;
    cbw.transferLength = scsiDataLength;
    cbw.flags = 0x80;
    cbw.command[0] = 0xBE;  // READ CD
    cbw.command[1] = 0x04;  // Expected sector type: CD-DA
    cbw.command[2] = (uint32_t)readCdLba >> 24;
    cbw.command[3] = (uint32_t)readCdLba >> 16;
    cbw.command[4] = (uint32_t)readCdLba >> 8;
    cbw.command[5] = (uint32_t)readCdLba;
    cbw.command[8] = 1;     // Transfer one sector
    cbw.command[9] = 0x10;  // Return user data (2352-byte CD-DA payload)
  }
  memcpy(botTransfer->data_buffer, &cbw, sizeof(cbw));

  if (command != ScsiCommand::ReadCd || readCdSectorsCompleted == 0) {
    Serial.printf("Sending SCSI %s...\n", scsiCommandName(command));
  }
  submitBotTransfer(bulkOutEndpoint, sizeof(cbw), BotPhase::Cbw);
}

static void startScsiSequence() {
  esp_err_t err = usb_host_interface_claim(usbClient, usbDevice, mscInterface, 0);
  if (err != ESP_OK) {
    Serial.printf("Could not claim Mass Storage interface: %s\n", esp_err_to_name(err));
    return;
  }

  err = usb_host_transfer_alloc(4096, 0, &botTransfer);
  if (err != ESP_OK) {
    Serial.printf("Could not allocate BOT transfer: %s\n", esp_err_to_name(err));
    usb_host_interface_release(usbClient, usbDevice, mscInterface);
    mscInterface = 0xFF;
    return;
  }
  startScsiCommand(ScsiCommand::Inquiry);
}

static const char *usbClassName(uint8_t classCode) {
  switch (classCode) {
    case 0x00: return "defined per interface";
    case 0x01: return "Audio";
    case 0x03: return "HID";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC Data";
    case 0xEF: return "Miscellaneous";
    case 0xFF: return "Vendor specific";
    default: return "unknown";
  }
}

static void printConfiguration(const usb_config_desc_t *config) {
  const uint8_t *p = config->val;
  const uint8_t *end = p + config->wTotalLength;
  bool massStorageFound = false;
  bool parsingMassStorage = false;

  Serial.printf("Configuration: %u interface(s), %u mA max\n", config->bNumInterfaces, config->bMaxPower * 2);

  while (p + 2 <= end) {
    const uint8_t length = p[0];
    const uint8_t type = p[1];
    if (length < 2 || p + length > end) {
      Serial.println("Malformed USB descriptor");
      break;
    }

    if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && length >= sizeof(usb_intf_desc_t)) {
      const usb_intf_desc_t *intf =
        reinterpret_cast<const usb_intf_desc_t *>(p);
      Serial.printf("  Interface %u alt %u: class 0x%02X (%s), subclass 0x%02X, protocol 0x%02X, endpoints %u\n",
                    intf->bInterfaceNumber, intf->bAlternateSetting, intf->bInterfaceClass,
                    usbClassName(intf->bInterfaceClass), intf->bInterfaceSubClass, intf->bInterfaceProtocol,
                    intf->bNumEndpoints);
      massStorageFound |= (intf->bInterfaceClass == 0x08);
      parsingMassStorage = intf->bInterfaceClass == 0x08 && intf->bInterfaceProtocol == 0x50;
      if (parsingMassStorage && mscInterface == 0xFF) {
        mscInterface = intf->bInterfaceNumber;
      }
    } else if (type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && length >= sizeof(usb_ep_desc_t)) {
      const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(p);
      Serial.printf("    Endpoint 0x%02X: attributes 0x%02X, max packet %u\n",
                    ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize);
      if (parsingMassStorage && (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK) {
        if (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
          bulkInEndpoint = ep->bEndpointAddress;
        } else {
          bulkOutEndpoint = ep->bEndpointAddress;
        }
      }
    }
    p += length;
  }

  if (massStorageFound) {
    Serial.println("RESULT: USB Mass Storage interface found (promising).");
    if (mscInterface != 0xFF && bulkInEndpoint != 0 && bulkOutEndpoint != 0) {
      startScsiSequence();
    } else {
      Serial.println("BOT endpoints were not found; INQUIRY not sent.");
    }
  } else {
    Serial.println("RESULT: device found, but no Mass Storage interface.");
  }
}

static void handleClientEvent(const usb_host_client_event_msg_t *event, void *) {
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    if (usbDevice != nullptr) {
      Serial.println("Another USB device was found; only one is handled for now.");
      return;
    }

    Serial.printf("\nUSB device connected at address %u\n", event->new_dev.address);
    playerStatus = PlayerStatus::CheckingDisc;
    esp_err_t err = usb_host_device_open(usbClient, event->new_dev.address, &usbDevice);
    if (err != ESP_OK) {
      Serial.printf("usb_host_device_open failed: %s\n", esp_err_to_name(err));
      return;
    }

    const usb_device_desc_t *deviceDesc = nullptr;
    err = usb_host_get_device_descriptor(usbDevice, &deviceDesc);
    if (err == ESP_OK) {
      Serial.printf("VID:PID = %04X:%04X\n", deviceDesc->idVendor, deviceDesc->idProduct);
      Serial.printf("USB %x.%02x, device class 0x%02X (%s), EP0 packet %u\n",
                    deviceDesc->bcdUSB >> 8, deviceDesc->bcdUSB & 0xFF, deviceDesc->bDeviceClass,
                    usbClassName(deviceDesc->bDeviceClass), deviceDesc->bMaxPacketSize0);
    } else {
      Serial.printf("Could not read device descriptor: %s\n", esp_err_to_name(err));
    }

    const usb_config_desc_t *configDesc = nullptr;
    err = usb_host_get_active_config_descriptor(usbDevice, &configDesc);
    if (err == ESP_OK) {
      printConfiguration(configDesc);
    } else {
      Serial.printf("Could not read configuration descriptor: %s\n", esp_err_to_name(err));
    }
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    if (event->dev_gone.dev_hdl == usbDevice) {
      Serial.println("\nUSB device disconnected.");
      ++discGeneration;
      cddbLookupRequested = false;
      cddbMetadataReady = false;
      cddbCandidatesReady = false;
      cddbCandidateCount = 0;
      cddbReadCandidateRequested = -1;
      cddbStatus = CddbStatus::Idle;
      cddbDiscTitle = "";
      for (String &title : cddbTrackTitles) title = "";
      playbackAbort = true;
      playbackPaused = false;
      pauseKeepAlive = false;
      tocTrackCount = 0;
      shuffleOrderCount = 0;
      shuffleOrderPosition = 0;
      playerStatus = PlayerStatus::WaitingDrive;
      botPhase = BotPhase::Idle;
      if (botTransfer != nullptr) {
        usb_host_transfer_free(botTransfer);
        botTransfer = nullptr;
      }
      if (mscInterface != 0xFF) {
        usb_host_interface_release(usbClient, usbDevice, mscInterface);
      }
      esp_err_t err = usb_host_device_close(usbClient, usbDevice);
      if (err != ESP_OK) {
        Serial.printf("usb_host_device_close failed: %s\n", esp_err_to_name(err));
      }
      usbDevice = nullptr;
      scsiCommandScheduled = false;
      mscInterface = 0xFF;
      bulkInEndpoint = 0;
      bulkOutEndpoint = 0;
    }
  }
}

struct UiButton {
  uint8_t pin;
  bool raw;
  bool stable;
  uint32_t changedAt;
  uint32_t pressedAt;
  uint32_t repeatedAt;
  bool pressEvent;
  bool releaseEvent;
  bool repeatEvent;
  bool longUsed;
};

// Explicit prototype prevents Arduino's auto-prototype generator from placing
// this declaration before the UiButton type definition.
static void updateButton(UiButton &button);

static UiButton uiButtons[] = {
  { BUTTON_UP_PIN, true, true, 0, 0, 0, false, false, false, false },
  { BUTTON_DOWN_PIN, true, true, 0, 0, 0, false, false, false, false },
  { BUTTON_ACTION_PIN, true, true, 0, 0, 0, false, false, false, false },
  { BUTTON_MODE_PIN, true, true, 0, 0, 0, false, false, false, false },
};

static void updateButton(UiButton &button) {
  button.pressEvent = false;
  button.releaseEvent = false;
  button.repeatEvent = false;
  const bool raw = digitalRead(button.pin);
  const uint32_t now = millis();
  if (raw != button.raw) {
    button.raw = raw;
    button.changedAt = now;
  }
  if (raw != button.stable && now - button.changedAt >= 30) {
    button.stable = raw;
    if (!raw) {
      button.pressEvent = true;
      button.pressedAt = now;
      button.repeatedAt = now;
      button.longUsed = false;
    } else {
      button.releaseEvent = true;
    }
  }
  //if (!button.stable && now - button.pressedAt >= 600 && now - button.repeatedAt >= 250) {
  // !button.longUsedを追加
  if (!button.stable && !button.longUsed && now - button.pressedAt >= 600 && now - button.repeatedAt >= 250) {
    button.repeatedAt = now;
    button.repeatEvent = true;
    button.longUsed = true;
  }
}

static void requestTrack(uint8_t index) {
  if (index >= tocTrackCount || !tocTracks[index].isAudio) return;
  if (shuffleEnabled) {
    if (shuffleOrderCount == 0) buildShuffleOrder(index);
    for (uint8_t i = 0; i < shuffleOrderCount; ++i) {
      if (shuffleOrder[i] == index) {
        shuffleOrderPosition = i;
        break;
      }
    }
  }

  if ((playbackFinished || readCdFinished) && botPhase == BotPhase::Idle && discLeadOutLba > tocTracks[index].startLba) {
    stoppedMediaPoll = false;
    scsiCommandScheduled = false;
    readCdLba = tocTracks[index].startLba;
    playbackStartLba = readCdLba;
    playbackTrackIndex = index;
    playbackPlayedLba = readCdLba;
    const int32_t endLba = (shuffleEnabled || repeatMode == RepeatMode::One) && index + 1 < tocTrackCount ? tocTracks[index + 1].startLba : discLeadOutLba;
    playbackSectorTarget = endLba - readCdLba;
    readCdSectorsCompleted = 0;
    readCdShortSectors = 0;
    readCdBytesReceived = 0;
    readCdHash = 2166136261UL;
    concealedSectorCount = 0;
    readCdStartedAt = millis();
    audioBytesWritten = 0;
    audioUnderrunChunks = 0;
    playbackPaused = false;
    playbackAbort = false;
    userStopRequested = false;
    playbackFinished = false;
    readCdFinished = false;
    displayedTrackIndex = index;
    if (audioBuffer != nullptr) xStreamBufferReset(audioBuffer);
    Serial.printf("Restarting from Track %u (LBA %ld).\n", tocTracks[index].number, (long)readCdLba);
    startScsiCommand(ScsiCommand::ReadCd);
    return;
  }

  requestedTrackIndex = index;
  requestedPlaybackLba = tocTracks[index].startLba;
  if (readPausedWaiting && botPhase == BotPhase::Idle) {
    readPausedWaiting = false;
    pauseKeepAlive = false;
    scsiCommandScheduled = false;
    startScsiCommand(ScsiCommand::ReadCd);
  }
}

static uint8_t trackIndexForLba(int32_t lba) {
  uint8_t index = 0;
  while (index + 1 < tocTrackCount && lba >= tocTracks[index + 1].startLba) {
    ++index;
  }
  return index;
}

static void applyPlayModeWithoutSeeking() {
  if (tocTrackCount == 0 || playbackFinished || playbackAbort || !discLeadOutFound) {
    return;
  }
  const uint8_t current = trackIndexForLba(playbackPlayedLba);
  playbackTrackIndex = current;
  const bool stopAtTrackEnd = shuffleEnabled || repeatMode == RepeatMode::One;
  const int32_t desiredEndLba = stopAtTrackEnd && current + 1 < tocTrackCount ? tocTracks[current + 1].startLba : discLeadOutLba;
  playbackSectorTarget = desiredEndLba - playbackStartLba;

  // If a previous single-track mode already stopped the reader while its
  // buffered audio is still draining, extending the boundary can resume from
  // the following sector without resetting audible position or the buffer.
  if (!stopAtTrackEnd && readCdFinished && botPhase == BotPhase::Idle && readCdLba + 1 < desiredEndLba) {
    readCdFinished = false;
    ++readCdLba;
    startScsiCommand(ScsiCommand::ReadCd);
  }
}

static void stopPlayback() {
  if (tocTrackCount == 0) return;
  userStopRequested = true;
  playbackAbort = true;
  playbackPaused = false;
  pauseKeepAlive = false;
  readPausedWaiting = false;
  scsiCommandScheduled = false;
  readCdFinished = true;
  playbackFinished = true;
  autoStartTrackIndex = -1;
  displayedTrackIndex = 0;
  playbackStartLba = tocTracks[0].startLba;
  playbackPlayedLba = tocTracks[0].startLba;
  playerStatus = PlayerStatus::Stopped;
  stoppedMediaPoll = true;
  Serial.println("STOP: returned to Track 1.");
}

static void requestRelativeSeek(int32_t sectors) {
  if (tocTrackCount == 0 || !discLeadOutFound) return;
  const int32_t firstLba = tocTracks[0].startLba;
  const int32_t target = constrain((int32_t)playbackPlayedLba + sectors, firstLba, discLeadOutLba - 1);
  const uint8_t trackIndex = trackIndexForLba(target);
  if (!tocTracks[trackIndex].isAudio) return;
  requestedTrackIndex = trackIndex;
  requestedPlaybackLba = target;
  Serial.printf("Seek request: %s 5 seconds -> LBA %ld.\n", sectors < 0 ? "back" : "forward", (long)target);
  if (readPausedWaiting && botPhase == BotPhase::Idle) {
    readPausedWaiting = false;
    pauseKeepAlive = false;
    scsiCommandScheduled = false;
    startScsiCommand(ScsiCommand::ReadCd);
  }
}
static void createCddbLookupTask(void) {
  // タイトル情報を再検索
  cddbMetadataReady = false;  //これを入れないと再検索中に♪マークが点滅しない
  Serial.println("[DEBUG] タイトル情報を再検索");
  cddbLookupRequested = false;
  cddbLookupRunning = true;
  if (xTaskCreate(cddbLookupTask, "cddb", 12288, nullptr, 1, nullptr) != pdPASS) {
    cddbLookupRunning = false;
    cddbLookupRequested = true;
    Serial.println("CDDB: could not start lookup task.");
  }
}

static void handleUiButtons() {
  for (UiButton &button : uiButtons) {
    updateButton(button);
  }
  UiButton &up = uiButtons[0];
  UiButton &down = uiButtons[1];
  UiButton &action = uiButtons[2];
  UiButton &mode = uiButtons[3];

  if (uiMode == UiMode::Cddb && cddbCandidatesReady && cddbCandidateCount > 0) {
    if (deleteDiskInfoProcessing == true) {
      if (action.releaseEvent && !action.longUsed) {
        // タイトルDBから削除
        if (deleteDiskInfo(mbdbDiscId.c_str()) == true) {
          mbdbDiscIdSaved = false;
          // タイトル情報を再検索
          createCddbLookupTask();
        }
        deleteDiskInfoProcessing = false;
        uiMode = UiMode::Track;
      } else if (mode.releaseEvent && !mode.longUsed) {
        // 削除キャンセル
        deleteDiskInfoProcessing = false;
        uiMode = UiMode::Track;
      }
      return;
    }
  }

  if (mode.pressEvent) {
    if (uiMode == UiMode::Track) uiMode = UiMode::Volume;
    else if (uiMode == UiMode::Volume) uiMode = UiMode::PlayMode;
    else if (uiMode == UiMode::PlayMode) uiMode = UiMode::Cddb;
    else {
      uiMode = UiMode::Track;
    }
  }

  if (uiMode == UiMode::Track && action.repeatEvent && !userStopRequested) {
    stopPlayback();
  }

  if (uiMode == UiMode::Track) {
    if (up.repeatEvent) requestRelativeSeek(-5 * 75);
    if (down.repeatEvent) requestRelativeSeek(5 * 75);
    if (up.releaseEvent && !up.longUsed) {
      if (shuffleEnabled && shuffleOrderPosition > 0) {
        requestTrack(shuffleOrder[shuffleOrderPosition - 1]);
      } else if (shuffleEnabled && shuffleOrderCount > 0) {
        requestTrack(shuffleOrder[shuffleOrderPosition]);
      } else if (!shuffleEnabled) {
        requestTrack(displayedTrackIndex > 0 ? displayedTrackIndex - 1 : 0);
      }
    }
    if (down.releaseEvent && !down.longUsed) {
      if (shuffleEnabled) {
        requestTrack(shuffleOrderPosition + 1 < shuffleOrderCount ? shuffleOrder[shuffleOrderPosition + 1] : shuffleOrder[0]);
      } else if (tocTrackCount > 0) {
        requestTrack(displayedTrackIndex + 1 < tocTrackCount ? displayedTrackIndex + 1 : 0);
      }
    }
    if (action.releaseEvent && !action.longUsed) {
      if (playbackFinished || readCdFinished) {
        Serial.println(" [Debug] STOP -> PLAY");
        requestTrack(displayedTrackIndex);
        return;
      }
      playbackPaused = !playbackPaused;
      Serial.println(playbackPaused ? "Paused." : "Playing.");
      if (!playbackPaused && readPausedWaiting && botPhase == BotPhase::Idle) {
        readPausedWaiting = false;
        pauseKeepAlive = false;
        scsiCommandScheduled = false;
        startScsiCommand(ScsiCommand::ReadCd);
      }
    }
  } else {
    if (uiMode == UiMode::Volume && (up.pressEvent || up.repeatEvent)) {
      volumePercent = min(100, (int)volumePercent + 5);
      volumeMuted = false;
    }
    if (uiMode == UiMode::Volume && (down.pressEvent || down.repeatEvent)) {
      volumePercent = max(0, (int)volumePercent - 5);
      volumeMuted = false;
    }
    if (uiMode == UiMode::Volume && action.releaseEvent && !action.longUsed) {
      volumeMuted = !volumeMuted;
    }
    if (uiMode == UiMode::PlayMode) {
      if (up.pressEvent) {
        shuffleEnabled = !shuffleEnabled;
        if (shuffleEnabled) {
          buildShuffleOrder(displayedTrackIndex);
        } else {
          shuffleOrderCount = 0;
          shuffleOrderPosition = 0;
        }
        applyPlayModeWithoutSeeking();
      }
      if (down.pressEvent) {
        if (repeatMode == RepeatMode::Off) repeatMode = RepeatMode::All;
        else if (repeatMode == RepeatMode::All) repeatMode = RepeatMode::One;
        else repeatMode = RepeatMode::Off;
        applyPlayModeWithoutSeeking();
      }
      if (action.releaseEvent && !action.longUsed) {
        const bool modeWasActive = shuffleEnabled || repeatMode != RepeatMode::Off;
        shuffleEnabled = false;
        repeatMode = RepeatMode::Off;
        shuffleOrderCount = 0;
        shuffleOrderPosition = 0;
        if (modeWasActive) applyPlayModeWithoutSeeking();
      }
    }
    //CDDBモード
    if (uiMode == UiMode::Cddb && cddbStatus != CddbStatus::Ready) {
      //タイトル情報がない場合
      if (action.releaseEvent && !action.longUsed) {
        //ACTIONボタン短押し
        // タイトル情報を再検索
        createCddbLookupTask();
        uiMode = UiMode::Track;
      }
    }
    if (uiMode == UiMode::Cddb && cddbCandidatesReady && cddbCandidateCount > 0) {
      if (up.pressEvent || up.repeatEvent) {
        cddbCandidateSelection = cddbCandidateSelection == 0 ? cddbCandidateCount - 1 : cddbCandidateSelection - 1;
      }
      if (down.pressEvent || down.repeatEvent) {
        cddbCandidateSelection = (cddbCandidateSelection + 1) % cddbCandidateCount;
      }
      if (action.releaseEvent && !action.longUsed) {
        //ACTIONボタン短押し
        // 候補変更
        Serial.printf("CDDB candidate %u selected: %s\n",
                      cddbCandidateSelection + 1, cddbCandidates[cddbCandidateSelection].title.c_str());
        cddbDiscTitle = cddbCandidates[cddbCandidateSelection].title;
        for (int i = 1; i <= tocTrackCount; i++) {
          cddbTrackTitles[i] = cddbCandidates[cddbCandidateSelection].titles[i];
        }
        uiMode = UiMode::Track;
      }
      if (action.releaseEvent && action.longUsed) {
        //ACTIONボタン長押し
        if (mbdbDiscIdSaved == true) {
          // タイトルDBに保存済の場合
          // タイトルDBから削除して、タイトル情報を再検索(確認付)
          if (mbdbDiscId != "") {
            deleteDiskInfoProcessing = true;
            return;
          }
        } else {
          // タイトルDBに未保存の場合
          // タイトルDBへ保存
          DiskInfo diskInfo;
          diskInfo.diskTitle = cddbDiscTitle;
          int arraySize = sizeof(cddbTrackTitles) / sizeof(cddbTrackTitles[0]);
          for (int i = 1; i <= arraySize; i++) {
            if (cddbTrackTitles[i] == "") {
              break;
            }
            diskInfo.trackTitles.push_back(cddbTrackTitles[i]);
          }
          if (saveOrUpdateDiskInfo(mbdbDiscId.c_str(), diskInfo) == true) {
            //保存済であることを継続的に表示するフラグを立てる
            mbdbDiscIdSaved = true;
            //保存操作が成功したことを一時的に表示するためのフラグを立てる
            mbdbDiscIdSavedShowMessage = true;
            mbdbDiscIdSavedPrevMillis = millis();
          }
        }
        uiMode = UiMode::Track;
      }
    }
  }
}

static const char *cddbStatusText(CddbStatus status) {
  switch (status) {
    case CddbStatus::Idle: return "IDLE";
    case CddbStatus::WaitingWifi: return "WAITING WI-FI";
    case CddbStatus::Querying: return "QUERYING";
    case CddbStatus::Reading: return "READING ENTRY";
    case CddbStatus::Ready: return "READY";
    case CddbStatus::NoMatch: return "NO MATCH";
    case CddbStatus::NetworkError: return "NETWORK ERROR";
  }
  return "UNKNOWN";
}

static size_t nextUtf8Byte(const String &text, size_t offset) {
  if (offset >= text.length()) return 0;
  ++offset;
  while (offset < text.length() && ((uint8_t)text[offset] & 0xC0) == 0x80) {
    ++offset;
  }
  return offset;
}

static void drawUi() {
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw < 100) return;
  lastDraw = millis();
  char line[24];

  const PlayerStatus status = playerStatus;
  if (status == PlayerStatus::WaitingDrive || status == PlayerStatus::CheckingDisc || status == PlayerStatus::WaitingDisc || status == PlayerStatus::SpinningUp || status == PlayerStatus::ReadingToc || status == PlayerStatus::Buffering || status == PlayerStatus::Error) {
    const char *statusText = "PLEASE WAIT     ";
    switch (status) {
      case PlayerStatus::WaitingDrive: statusText = "WAITING DRIVE   "; break;
      case PlayerStatus::CheckingDisc: statusText = "CHECKING DISC   "; break;
      case PlayerStatus::WaitingDisc: statusText = "NO DISC         "; break;
      case PlayerStatus::SpinningUp: statusText = "SPINNING UP     "; break;
      case PlayerStatus::ReadingToc: statusText = "READING TOC     "; break;
      case PlayerStatus::Buffering: statusText = "BUFFERING AUDIO "; break;
      case PlayerStatus::Error: statusText = "ERROR           "; break;
      default: break;
    }
    lcd.setCursor(0, 0);
    lcd.print(statusText);

    return;
  }

  //CDDB候補を表示する
  if (uiMode == UiMode::Cddb) {
    if (cddbCandidateCount == 0) { // 20261031.00 追加
      lcd.setCursor(0, 0);
      lcd.print("00:             ");
    } else {
      const uint8_t candidateCount = cddbCandidateCount;
      const uint8_t requestedSelection = cddbCandidateSelection;
      const uint8_t selected = min(requestedSelection, (uint8_t)(candidateCount - 1));
      lcd.setCursor(0, 0);
      snprintf(line, sizeof(line), "%02d:%-13s",
               selected + 1, cddbCandidates[selected].title.substring(0, 12).c_str());
      if (deleteDiskInfoProcessing == true) {
        if (((millis() / 1000) & 1) == 0) {
          lcd.print(">>>  Delete? <<<");
        } else {
          lcd.print(line);
        }
        lcd.setCursor(0, 1);
        lcd.print("ACT:Yes/MODE:No ");
        return;
      }
      lcd.print(line);
    } 
  }

  lcd.setCursor(13, 1);
  switch (uiMode) {
    case UiMode::Track: lcd.print("TRK"); break;
    case UiMode::Volume: lcd.print("VOL"); break;
    case UiMode::PlayMode: lcd.print("OPT"); break;
    case UiMode::Cddb: lcd.print("CDB"); break;
    default: break;
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)((cddbLookupRunning && ((millis() / 350) & 1) == 0) || cddbMetadataReady ? Icon::MusicalNote : Icon::Underbar));

  const uint8_t trackIndex = tocTrackCount == 0 ? 0 : min(displayedTrackIndex, (uint8_t)(tocTrackCount - 1));
  const int32_t trackStart = tocTrackCount == 0 ? 0 : tocTracks[trackIndex].startLba;
  const int32_t trackEnd = tocTrackCount == 0 ? 1 : (trackIndex + 1 < tocTrackCount ? tocTracks[trackIndex + 1].startLba : discLeadOutLba);
  const int32_t position = constrain((int32_t)playbackPlayedLba, trackStart, trackEnd);
  const uint32_t elapsedSeconds = position > trackStart ? (position - trackStart) / 75 : 0;
  const uint32_t durationSectors = max(1L, trackEnd - trackStart);
  const uint8_t progress = (uint32_t)(position - trackStart) * 124 / durationSectors;

  // lcd.createChar()は処理に時間が掛かるため、playerStatusに変化があった時だけユーザー登録文字を書き換える
  if (playerStatus != prevPlayerStatus) {
    prevPlayerStatus = playerStatus;
    switch (playerStatus) {
      case PlayerStatus::Playing: lcd.createChar(uint8_t(Icon::Status), playing); break;
      case PlayerStatus::Paused: lcd.createChar(uint8_t(Icon::Status), paused); break;
      case PlayerStatus::Stopped: lcd.createChar(uint8_t(Icon::Status), stopped); break;
      default: break;
    }
  }
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)Icon::Status);

  snprintf(line, sizeof(line), "%02lu:%02lu ",
           (unsigned long)(elapsedSeconds / 60), (unsigned long)(elapsedSeconds % 60));
  lcd.setCursor(1, 1);
  lcd.print(line);

  //lcd.print(WiFi.status() == WL_CONNECTED ? "W" : "_");
  lcd.setCursor(7, 1);
  lcd.write((uint8_t)(WiFi.status() == WL_CONNECTED ? Icon::WiFi : Icon::Underbar));
  lcd.setCursor(9, 1);
  lcd.write((uint8_t)(mbdbDiscIdSaved ? Icon::Saved : Icon::Underbar));
  //lcd.print(shuffleEnabled ? "S" : "_");
  lcd.write((uint8_t)(shuffleEnabled ? Icon::Shuffle : Icon::Underbar));
  //lcd.print(repeatMode == RepeatMode::Off ? "_" : repeatMode == RepeatMode::One ? "1" : "A");
  lcd.write((uint8_t)(repeatMode == RepeatMode::Off ? Icon::Underbar : repeatMode == RepeatMode::One ? Icon::RepeatOne
                                                                                                     : Icon::RepeatAll));
  lcd.print(" ");

  if (mbdbDiscIdSavedShowMessage == true) {
    lcd.setCursor(0, 0);
    lcd.print("> Title Saved! <");
    if (millis() - mbdbDiscIdSavedPrevMillis > 1000) {
      mbdbDiscIdSavedShowMessage = false;
    }
    return;
  }

  if (uiMode == UiMode::Cddb) {
    return;
  }

  // MusicBrainzから取得したアルバム名・曲名を表示
  const bool showMetadata = uiMode == UiMode::Track && cddbMetadataReady && ((status == PlayerStatus::Stopped && cddbDiscTitle.length() != 0) || cddbTrackTitles[trackIndex + 1].length() != 0);
  static int loop = 0;          //ここを通る回数をカウント
  static int loopMax = 0;       //スクロールスピード調節(0:一番速い、1以上:遅くなる)
  static int loopMaxCount = 0;  //loopがloopMaxを超えた回数(loopMaxCountが1増える⇒1文字スクールさせる)
  static int from = 0;          //(タイトル全体から)表示する文字列を切り出す開始位置
  static unsigned long prevMillis = 0;
  static int32_t prevDiscGeneration = 0;
  static int prevTrackIndex = 0;
  static int prevCandidateSelection = 0;
  static UiMode prevUiMode = UiMode::PlayMode;
  static PlayerStatus prevStatus = PlayerStatus::Stopped;
  static char metadata[17];

  if (showMetadata) {
    // 停止した時にタイトルを表示させる
    if (prevStatus != status) {
      prevStatus = status;
      if (status == PlayerStatus::Stopped) {
        prevMillis = millis();
      }
    }
    // ディスク入替があったらタイトルを表示させる
    if (prevDiscGeneration != discGeneration) {
      prevDiscGeneration = discGeneration;
      prevMillis = millis();
    }
    // トラック移動した時にトラック名を表示開始させる
    if (prevTrackIndex != trackIndex) {
      prevTrackIndex = trackIndex;
      loop = 0;
      loopMax = 0;
      from = loopMaxCount = 0;
      prevMillis = millis();
    }
    if ((millis() - prevMillis) < 30000UL) {
      if (loop > loopMax) {
        if (status == PlayerStatus::Stopped) {
          tmpMetadata = cddbDiscTitle;
        } else {
          snprintf(line, sizeof(line), "%02d:", trackIndex + 1);
          tmpMetadata = String(line) + cddbTrackTitles[trackIndex + 1];
        }
        loop = 0;
        snprintf(metadata, sizeof(metadata), "%-16s", tmpMetadata.substring(from, from + 16).c_str());
        lcd.setCursor(0, 0);
        lcd.print(metadata);
        if (loopMaxCount + 16 < tmpMetadata.length()) {
          // temp文字列の右端までスクロールしきっていないため1文字スクロール
          loopMaxCount++;
          from = loopMaxCount;
        } else {
          from = 0;  // 右端までスクロールしたため元に戻す
        }
        // スクロールの最初と最後たけ表示を長くする
        loopMax = loopMaxCount == 1 ? 30 : from == 0 ? 30 : 5;
      }
      loop++;
      return;
    }
  } else {
    loop = 0;
    loopMax = 0;
    from = loopMaxCount = 0;
    prevMillis = millis();
  }

  snprintf(line, sizeof(line), "TRK%02u/%02u ", tocTrackCount ? tocTracks[trackIndex].number : 0, tocTrackCount);
  lcd.setCursor(0, 0);
  lcd.print(line);

  char vol[] = "MUTED  ";
  if (!volumeMuted) {
    snprintf(vol, sizeof(vol), "VOL%3u%%", volumePercent);
  }
  lcd.setCursor(9, 0);
  lcd.print(vol);
}

void lcd_clear() {
  lcd.clear();
  delay(6);
  lcd.home();
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.createChar(uint8_t(Icon::Status), playing);
  lcd.createChar(uint8_t(Icon::WiFi), wifi);
  lcd.createChar(uint8_t(Icon::MusicalNote), musicalNote);
  lcd.createChar(uint8_t(Icon::Saved), saved);
  lcd.createChar(uint8_t(Icon::Underbar), underbar);
  lcd.createChar(uint8_t(Icon::Shuffle), shuffle);
  lcd.createChar(uint8_t(Icon::RepeatOne), repeatOne);
  lcd.createChar(uint8_t(Icon::RepeatAll), repeatAll);
  lcd_clear();
  lcd.setCursor(0, 0);
  lcd.print(PRGNAME);
  lcd.setCursor(0, 1);
  lcd.print(VERSION);
  Serial.printf("\n%s %s\n", PRGNAME, VERSION);
  delay(1500);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFSの初期化に失敗しました。");
  }

  Serial.println("ESP32-S3 USB CD drive detection test");
  randomSeed(esp_random());
  WiFi.onEvent(handleWifiEvent);
  WiFi.mode(WIFI_STA);

  for (UiButton &button : uiButtons) {
    pinMode(button.pin, INPUT_PULLUP);
  }
  lcd_clear();

  startI2sAudio();

// ESP32-S3-USB-OTG board以外は #define USB_HOST_EN しない
#ifdef USB_HOST_EN
  // ESP32-S3-USB-OTG board: route D+/D- to the HOST connector and enable
  // its current-limited VBUS path. Other ESP32-S3 boards do not define this.
  usbHostEnable(true);
  usbHostPower(USB_HOST_POWER_VBUS);
  delay(100);
#endif

  usb_host_config_t hostConfig = {};
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK) {
    Serial.printf("usb_host_install failed: %s\n", esp_err_to_name(err));
    return;
  }

  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 5;
  clientConfig.async.client_event_callback = handleClientEvent;
  err = usb_host_client_register(&clientConfig, &usbClient);
  if (err != ESP_OK) {
    Serial.printf("usb_host_client_register failed: %s\n", esp_err_to_name(err));
    return;
  }

  Serial.println("USB Host ready. Connect the externally powered CD drive.");

  tmpMetadata.reserve(128);
}

void loop() {
  serviceWifiAndCddb();
  if (usbClient == nullptr) {
    delay(1000);
    return;
  }

  // Both the host library and this client must be serviced regularly.
  uint32_t eventFlags = 0;
  usb_host_lib_handle_events(1, &eventFlags);
  usb_host_client_handle_events(usbClient, 1);
  handleUiButtons();
  drawUi();

  if (autoStartTrackIndex >= 0 && botPhase == BotPhase::Idle) {
    const uint8_t next = autoStartTrackIndex;
    autoStartTrackIndex = -1;
    requestTrack(next);
  }

  if (playerStatus == PlayerStatus::Stopped && stoppedMediaPoll && botPhase == BotPhase::Idle && !scsiCommandScheduled && usbDevice != nullptr) {
    scheduleScsiCommand(ScsiCommand::TestUnitReady, 1000);
  }

  // A tray-open/reset sequence can occasionally abort an early INQUIRY or
  // BOT status transfer. If that leaves CHECKING DISC with no command in
  // flight, resume with TEST UNIT READY instead of displaying it forever.
  if (playerStatus == PlayerStatus::CheckingDisc && botPhase == BotPhase::Idle && !scsiCommandScheduled && usbDevice != nullptr && botTransfer != nullptr) {
    Serial.println("Recovering stalled disc check with TEST UNIT READY...");
    scheduleScsiCommand(ScsiCommand::TestUnitReady, 500);
  }

  if (scsiCommandScheduled && botPhase == BotPhase::Idle && usbDevice != nullptr && (int32_t)(millis() - scsiCommandDueAt) >= 0) {
    scsiCommandScheduled = false;
    startScsiCommand(scheduledScsiCommand);
  }
  delay(1);
}
