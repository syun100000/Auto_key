#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
// ピンの宣言
#define RELAY 8
#define MONITOR 9
#define BUTTON 2

#define LOCKED_STATE_DELAY 30000    // 施錠と判断するまでのLOW状態の持続時間 (ミリ秒)
#define UNLOCKED_STATE_DELAY 30000  // 解錠と判断するまでのHIGH状態の持続時間 (ミリ秒)


// MAC、IP、ポートの宣言
byte mac[] = { 0x48, 0xE9, 0xF1, 0x1D, 0xE6, 0x76 };
byte ip[] = { 192, 168, 1, 19};
EthernetServer server(80);

// 制御変数
unsigned long lockedStateStartTime = 0;
unsigned long unlockedStateStartTime = 0;

unsigned long lockTimeout = 0;
boolean locked = true;
int autoLockTime = 4;                                                      // Default is no autolock.
unsigned long lockTimes[] = { 30000, 60000, 120000, 180000, 0xFFFFFFFF };  // ミリ秒単位
String lockTimeNames[] = { "30秒", "1分", "2分", "3分", "無効" };
// int ledPins[] = {LEDS};
int ledPins[] = { 4, 5, 6, 7 };
int lock_led = 3;

void setup() {
  pinMode(RELAY, OUTPUT);
  pinMode(MONITOR, INPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(lock_led, OUTPUT);
  // シリアル通信の設定
  Serial.begin(9600);
  Serial.println("起動しました。");
  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
  }
  // EEPROMから時間設定を読み取る
  autoLockTime = EEPROM.read(0);
  if (autoLockTime >= 5) {  // 無効な値の場合、デフォルト値を設定
    autoLockTime = 4;
  }
  Ethernet.begin(mac, ip);
  Serial.println(Ethernet.localIP());
  Serial.println("現在のボタンの状態" + String(autoLockTime));
  server.begin();
  // autoLockTimeに応じたLEDを点灯
  digitalWrite(ledPins[autoLockTime], HIGH);
  // 起動準備のため5秒待つ
  delay(5000);
}

#define DEBOUNCE_DELAY 10            // デバウンス時間をミリ秒で設定
int lastButtonState = HIGH;          // 前回のボタンの状態
int buttonState;                     // 現在のボタンの状態
unsigned long lastDebounceTime = 0;  // ボタンの状態が最後に切り替わった時刻
// boolean previousLockedState = true; // ロックの初期状態を設定
// ロックの状態を保存する変数 （1 = 施錠、0 = 解錠）
int previousLockedState = 0;
void loop() {
  int currentMonitorState = digitalRead(MONITOR);
  // boolean currentLockedState = (currentMonitorState == HIGH);
  if (currentMonitorState == 1){
    //モニターが施錠中判定
    locked = 1;
  }
  else if(currentMonitorState == 0){
    locked = 0;
  }

  if (previousLockedState != locked){
    // sendLockStateToHomebridge(locked);
    if (locked == 1){
      //施錠中の処理
      sendLockStateToHomebridge(true);
    }
    else if (locked == 0){
      //解錠中の処理
      sendLockStateToHomebridge(false);
    }
    // 状態を保存
    previousLockedState = locked;
  }
  // // ロックの状態が変更されたかチェック
  // if (currentLockedState != previousLockedState) {
  //   // ロックの状態が変更された場合、Homebridgeに通知
  //   sendLockStateToHomebridge(currentLockedState);

  //   // 現在の状態を保存
  //   previousLockedState = currentLockedState;
  // }




  // ボタンの現在の状態を読み取る
  int reading = digitalRead(BUTTON);

  // ボタンの状態が変わった場合、デバウンスタイマーをリセット
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // ボタンの状態がDEBOUNCE_DELAY時間以上安定している場合、ボタンの状態を更新
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // ボタンの状態が変化している場合
    if (reading != buttonState) {
      buttonState = reading;
      // ボタンが押された場合、自動施錠時間を切り替える
      if (buttonState == LOW) {
        autoLockTime = (autoLockTime + 1) % 5;  // 0から4の間で循環
        // EEPROMに時間設定を保存
        EEPROM.write(0, autoLockTime);
        Serial.println("ボタンが押されました。");
        Serial.println("現在のボタンの状態: " + String(autoLockTime));
        // すべてのLEDを消灯
        for (int i = 0; i < 4; i++) {
          digitalWrite(ledPins[i], LOW);
        }
        // autoLockTimeに応じたLEDを点灯
        digitalWrite(ledPins[autoLockTime], HIGH);
      }
    }
  }

  // 最後の読み取りを前回のボタンの状態として保存
  lastButtonState = reading;
  //現在の施錠状態を確認し、LEDに出力する。
  if (locked) {
    //施錠中の処理
    if (lockTimeout != 0){
      lockTimeout = 0;
    }
    digitalWrite(lock_led, LOW);
  } else if (!locked) {
    //解説中の処理
    if (lockTimeout == 0){
     lockTimeout = millis() + lockTimes[autoLockTime]; 
    }
    digitalWrite(lock_led, HIGH);
  }
  EthernetClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        String command = client.readStringUntil(' ');
        if (command.startsWith("/?status")) {
          // 現在の施錠状態をJSON形式で返す
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.println();
          client.println("{\"locked\":" + String(locked ? "true" : "false") + "}");
        }
        if (command.startsWith("/?autoLockTime=")) {
          autoLockTime = command.substring(15).toInt();
          // EEPROMに時間設定を保存
          EEPROM.write(0, autoLockTime);
          // すべてのLEDを消灯
          for (int i = 0; i < 4; i++) {
            digitalWrite(ledPins[i], LOW);
          }
          // autoLockTimeに応じたLEDを点灯
          digitalWrite(ledPins[autoLockTime], HIGH);
        } else if (command.startsWith("/?action=")) {
          int action = command.substring(9).toInt();
          if (action == 0 && locked) {
            //解錠のGETが送信された場合
            digitalWrite(RELAY, HIGH);  // 解錠
            delay(200);                 // 0.2秒間制御信号を送る
            digitalWrite(RELAY, LOW);
            lockTimeout = millis() + lockTimes[autoLockTime];
            locked = 0;
          } else if (action == 1 && !locked) {
            //施錠するGETが送信された場合
            digitalWrite(RELAY, HIGH);  // 施錠
            delay(200);                 // 0.2秒間制御信号を送る
            digitalWrite(RELAY, LOW);
            locked = 1;
          }
        }
      } else {
        returnHTML(client, locked);
        break;
      }
    }
    client.stop();
  }

  // 自動施錠機能
  if (!locked && millis() >= lockTimeout) {
    while(digitalRead(MONITOR) == LOW){
      digitalWrite(RELAY, HIGH);  // 施錠
      delay(200);                // 1秒間制御信号を送る
      digitalWrite(RELAY, LOW);
      delay(5000);
    }

    Serial.println("自動施錠を実行しました。");
    lockTimeout = 0;
  }
  Serial.println("現在の施錠解錠状態" + String(locked));
  Serial.println(lockTimeout);
}



void returnHTML(EthernetClient client, boolean locked) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println();

  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
  client.println("<head>");
  client.println("<meta charset='utf-8'/>");
  client.println("<meta name=viewport content=\"width=80px, initial-scale=4, maximum-scale=4, user-scalable=no\" />");
  client.println("<title>電気錠操作システム</title>");
  client.println("</head>");
  client.println("<body style=\"color:rgb(205,205,205);background-color:rgb(96,96,96);text-align:center;\">");
  client.println("<h3>電気錠操作システム</31>");
  client.println("<h4>現在の状態: " + String(locked ? "施錠" : "解錠") + "</h4>");
  client.println("<h4>自動施錠時間: " + lockTimeNames[autoLockTime] + "</h4>");
  client.println("<form method=GET>");
  client.println("<select name=autoLockTime onchange='this.form.submit()'>");
  for (int i = 0; i < 5; i++) {
    client.println("<option value=" + String(i) + (i == autoLockTime ? " selected" : "") + ">" + lockTimeNames[i] + "</option>");
  }
  client.println("</select>");
  client.println("</form>");
  client.println("<form method=GET>");
  if (!locked) {
    client.println("<input type=hidden name=action value=1>");
    client.println("<input type=submit value=施錠 />");
  } else {
    client.println("<input type=hidden name=action value=0>");
    client.println("<input type=submit value=解錠 />");
  }
  client.println("</form>");
  client.println("</body>");
  client.println("</html>");
}

// ホームブリッジに状態を送信する関数

// Homebridgeサーバーにロックの状態を送信する関数
void sendLockStateToHomebridge(boolean locked) {
  char server[] = "homebridge.local";  // Homebridgeサーバーのホスト名
  int port = 51828;                    // Homebridgeサーバーのポート

  EthernetClient client;
  Serial.println("Homebridgeサーバーに接続しています...");

  if (client.connect(server, port)) {
    Serial.println("Homebridgeサーバーに接続しました。");

    // HTTPリクエストを構築
    String url = "/?accessoryId=entrance_lock_1&lockcurrentstate=";
    url += locked ? "1" : "0";  // 施錠は1、解錠は0

    // HTTP GETリクエストを送信
    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: " + String(server));
    client.println("Connection: close");
    client.println();  // リクエストの終わりを示す空行

    // 応答を待つ（オプション）
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
      }
    }

    // 接続を閉じる
    client.stop();
    Serial.println("\n送信完了");
  } else {
    // 接続に失敗した場合
    Serial.println("エラー: Homebridgeサーバーに接続できませんでした。");
  }
}


// 例: ロック状態が変更された場合にこの関数を呼び出す
// sendLockStateToHomebridge(true); // 施錠
// sendLockStateToHomebridge(false); // 解錠
