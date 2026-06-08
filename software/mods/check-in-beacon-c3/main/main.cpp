#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <string.h>

#if __has_include("beacon_config.generated.h")
#include "beacon_config.generated.h"
#elif __has_include("beacon_config.h")
#include "beacon_config.h"
#endif

#ifndef BEACON_WIFI_SSID
#define BEACON_WIFI_SSID "CIC Guest"
#endif
#ifndef BEACON_WIFI_PASSWORD
#define BEACON_WIFI_PASSWORD "1nnovation"
#endif
#ifndef BEACON_SERVER_URL
#define BEACON_SERVER_URL "https://oniondao.dev/api/badge/checkin"
#endif
#ifndef BEACON_SERVER_CA_PEM
#define BEACON_SERVER_CA_PEM ""
#endif
#ifndef BEACON_SHARED_SECRET
#define BEACON_SHARED_SECRET "02eb3d5e04fd2dc9cbb6f3f5c6c9d89d9c96acd987cc24ddf9f717f9480e8786"
#endif
#ifndef BEACON_ID
#define BEACON_ID "beacon-c3-001"
#endif
#ifndef BEACON_ROOM
#define BEACON_ROOM "front-desk"
#endif
#ifndef BEACON_LABEL
#define BEACON_LABEL "Front Desk Check In"
#endif
#ifndef BEACON_ESPNOW_CHANNEL
#define BEACON_ESPNOW_CHANNEL 6
#endif
#ifndef BEACON_RSSI_THRESHOLD_DBM
#define BEACON_RSSI_THRESHOLD_DBM -75
#endif
#ifndef BEACON_ESPNOW_PMK
#define BEACON_ESPNOW_PMK "onion-checkin-01"
#endif
#ifndef BEACON_ADVERTISE_INTERVAL_MS
#define BEACON_ADVERTISE_INTERVAL_MS 1000
#endif
#ifndef BEACON_DEFAULT_POINTS
#define BEACON_DEFAULT_POINTS 0
#endif

static constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static constexpr char kMagic[6] = {'O', 'N', 'C', 'H', 'K', '1'};
static constexpr uint8_t kProtocolVersion = 1;
static constexpr uint8_t kTypeAdvertise = 1;
static constexpr uint8_t kTypeApprove = 2;
static constexpr uint8_t kTypeResult = 3;
static constexpr uint8_t kRxQueueLen = 8;
static constexpr uint32_t kWifiRetryMs = 15000;
static constexpr uint32_t kHttpTimeoutMs = 5000;

struct __attribute__((packed)) CheckInHeader {
    char magic[6];
    uint8_t version;
    uint8_t type;
};

struct __attribute__((packed)) AdvertisePacket {
    CheckInHeader header;
    char beaconId[32];
    char room[32];
    char label[48];
    int8_t minRssi;
    uint8_t nonce[8];
    uint32_t sequence;
};

struct __attribute__((packed)) ApprovePacket {
    CheckInHeader header;
    char beaconId[32];
    uint8_t nonce[8];
    char hardwareId[65];
    uint64_t onionId;
    char username[32];
    char wallet[48];
    int8_t rssi;
    uint32_t approvedAt;
    uint8_t badgeMac[6];
};

struct __attribute__((packed)) ResultPacket {
    CheckInHeader header;
    char beaconId[32];
    uint8_t nonce[8];
    uint8_t awarded;
    uint16_t points;
    char message[80];
};

static_assert(sizeof(AdvertisePacket) <= ESP_NOW_MAX_DATA_LEN, "AdvertisePacket exceeds ESP-NOW payload limit");
static_assert(sizeof(ApprovePacket) <= ESP_NOW_MAX_DATA_LEN, "ApprovePacket exceeds ESP-NOW payload limit");
static_assert(sizeof(ResultPacket) <= ESP_NOW_MAX_DATA_LEN, "ResultPacket exceeds ESP-NOW payload limit");

struct RxPacket {
    uint8_t mac[6];
    uint8_t len;
    uint8_t payload[ESP_NOW_MAX_DATA_LEN];
    int8_t receivedRssi;
    uint32_t receivedAt;
};

struct PostResult {
    bool ok;
    uint16_t points;
    char message[80];
    int httpCode;
};

static esp_err_t httpCaptureEvent(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->user_data && event->data && event->data_len > 0) {
        String* response = static_cast<String*>(event->user_data);
        response->concat(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

static portMUX_TYPE s_rxMux = portMUX_INITIALIZER_UNLOCKED;
static RxPacket s_rxQueue[kRxQueueLen];
static uint8_t s_rxHead = 0;
static uint8_t s_rxCount = 0;
static uint8_t s_nonce[8];
static uint32_t s_sequence = 0;
static uint32_t s_lastAdvertise = 0;
static uint32_t s_lastWifiAttempt = 0;
static bool s_espnowStarted = false;

static bool isPlaceholder(const char* value) {
    return value == nullptr || value[0] == '\0' || strcmp(value, "CHANGE_ME") == 0;
}

static void initHeader(CheckInHeader& header, uint8_t type) {
    memcpy(header.magic, kMagic, sizeof(header.magic));
    header.version = kProtocolVersion;
    header.type = type;
}

static bool validHeader(const CheckInHeader& header, uint8_t type) {
    return memcmp(header.magic, kMagic, sizeof(header.magic)) == 0 &&
           header.version == kProtocolVersion &&
           header.type == type;
}

static void copyFixed(char* dest, size_t destLen, const char* src) {
    memset(dest, 0, destLen);
    if (!src || destLen == 0) return;
    snprintf(dest, destLen, "%s", src);
}

static String fixedString(const char* src, size_t len) {
    size_t used = 0;
    while (used < len && src[used] != '\0') used++;
    String out;
    out.reserve(used);
    for (size_t i = 0; i < used; i++) out += src[i];
    return out;
}

static String macToString(const uint8_t mac[6]) {
    char out[18];
    snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(out);
}

static String nonceToHex(const uint8_t nonce[8]) {
    char out[17];
    for (uint8_t i = 0; i < 8; i++) snprintf(out + i * 2, 3, "%02X", nonce[i]);
    return String(out);
}

static bool nonceMatches(const uint8_t nonce[8]) {
    return memcmp(nonce, s_nonce, sizeof(s_nonce)) == 0;
}

static void rxQueuePush(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
    if (!mac || !data || len <= 0) return;
    if (len > ESP_NOW_MAX_DATA_LEN) len = ESP_NOW_MAX_DATA_LEN;

    portENTER_CRITICAL(&s_rxMux);
    uint8_t slot = (s_rxHead + s_rxCount) % kRxQueueLen;
    if (s_rxCount == kRxQueueLen) {
        slot = s_rxHead;
        s_rxHead = (s_rxHead + 1) % kRxQueueLen;
    } else {
        s_rxCount++;
    }

    RxPacket& packet = s_rxQueue[slot];
    memcpy(packet.mac, mac, sizeof(packet.mac));
    packet.len = (uint8_t)len;
    memcpy(packet.payload, data, len);
    packet.receivedRssi = rssi;
    packet.receivedAt = millis();
    portEXIT_CRITICAL(&s_rxMux);
}

static bool rxQueuePop(RxPacket& out) {
    bool ok = false;
    portENTER_CRITICAL(&s_rxMux);
    if (s_rxCount > 0) {
        out = s_rxQueue[s_rxHead];
        s_rxHead = (s_rxHead + 1) % kRxQueueLen;
        s_rxCount--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_rxMux);
    return ok;
}

static void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    int8_t rssi = 0;
    if (info && info->rx_ctrl) rssi = info->rx_ctrl->rssi;
    if (info && info->src_addr) rxQueuePush(info->src_addr, data, len, rssi);
}

static bool addPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t rc = esp_now_add_peer(&peer);
    if (rc != ESP_OK) {
        Serial.printf("[beacon] ESP-NOW add peer %s failed: %d\n", macToString(mac).c_str(), (int)rc);
        return false;
    }
    return true;
}

static bool sendPacket(const uint8_t mac[6], const void* packet, size_t len) {
    if (len == 0 || len > ESP_NOW_MAX_DATA_LEN) return false;
    if (!addPeer(mac)) return false;
    esp_err_t rc = esp_now_send(mac, reinterpret_cast<const uint8_t*>(packet), len);
    if (rc != ESP_OK) {
        Serial.printf("[beacon] ESP-NOW send %s failed: %d\n", macToString(mac).c_str(), (int)rc);
        return false;
    }
    return true;
}

static bool connectWiFi(uint32_t timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (isPlaceholder(BEACON_WIFI_SSID)) {
        Serial.println("[beacon] WiFi credentials not configured; POST disabled");
        return false;
    }

    Serial.printf("[beacon] connecting WiFi SSID=%s\n", BEACON_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(BEACON_WIFI_SSID, BEACON_WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(200);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[beacon] WiFi connect failed");
        return false;
    }

    uint8_t channel = WiFi.channel();
    Serial.printf("[beacon] WiFi up ip=%s channel=%u\n",
                  WiFi.localIP().toString().c_str(), (unsigned)channel);
    if (BEACON_ESPNOW_CHANNEL > 0 && channel != BEACON_ESPNOW_CHANNEL) {
        Serial.printf("[beacon] warning: AP channel %u differs from configured ESP-NOW channel %u\n",
                      (unsigned)channel, (unsigned)BEACON_ESPNOW_CHANNEL);
    }
    return true;
}

static bool ensureEspNow() {
    if (s_espnowStarted) return true;

    WiFi.mode(WIFI_STA);
    if (WiFi.status() != WL_CONNECTED && BEACON_ESPNOW_CHANNEL > 0) {
        esp_wifi_set_promiscuous(true);
        esp_err_t channelRc = esp_wifi_set_channel((uint8_t)BEACON_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
        if (channelRc != ESP_OK) {
            Serial.printf("[beacon] set ESP-NOW channel failed: %d\n", (int)channelRc);
            return false;
        }
    }

    esp_err_t rc = esp_now_init();
    if (rc != ESP_OK) {
        Serial.printf("[beacon] ESP-NOW init failed: %d\n", (int)rc);
        return false;
    }

    uint8_t pmk[ESP_NOW_KEY_LEN] = {};
    size_t pmkLen = strlen(BEACON_ESPNOW_PMK);
    if (pmkLen > sizeof(pmk)) pmkLen = sizeof(pmk);
    memcpy(pmk, BEACON_ESPNOW_PMK, pmkLen);
    rc = esp_now_set_pmk(pmk);
    if (rc != ESP_OK) {
        Serial.printf("[beacon] ESP-NOW PMK failed: %d\n", (int)rc);
    }

    esp_now_register_recv_cb(onEspNowReceive);
    addPeer(kBroadcastMac);
    s_espnowStarted = true;

    uint8_t channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&channel, &second);
    Serial.printf("[beacon] ESP-NOW ready mac=%s channel=%u minRssi=%d dBm\n",
                  WiFi.macAddress().c_str(), (unsigned)channel, BEACON_RSSI_THRESHOLD_DBM);
    return true;
}

static void advertise() {
    if (!s_espnowStarted) return;

    AdvertisePacket packet = {};
    initHeader(packet.header, kTypeAdvertise);
    copyFixed(packet.beaconId, sizeof(packet.beaconId), BEACON_ID);
    copyFixed(packet.room, sizeof(packet.room), BEACON_ROOM);
    copyFixed(packet.label, sizeof(packet.label), BEACON_LABEL);
    packet.minRssi = BEACON_RSSI_THRESHOLD_DBM;
    memcpy(packet.nonce, s_nonce, sizeof(packet.nonce));
    packet.sequence = ++s_sequence;

    sendPacket(kBroadcastMac, &packet, sizeof(packet));
    Serial.printf("[beacon] advertise seq=%u id=%s room=%s minRssi=%d\n",
                  (unsigned)packet.sequence, BEACON_ID, BEACON_ROOM, packet.minRssi);
}

static void setDefaultPostResult(PostResult& result) {
    result.ok = false;
    result.points = BEACON_DEFAULT_POINTS;
    result.httpCode = -1;
    copyFixed(result.message, sizeof(result.message), "Check-in failed");
}

static void applyServerResult(PostResult& result, const String& response) {
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) return;

    String status;
    cJSON* statusJson = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(statusJson) && statusJson->valuestring) {
        status = statusJson->valuestring;
    }

    cJSON* ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    if (cJSON_IsBool(ok)) result.ok = cJSON_IsTrue(ok);

    cJSON* awarded = cJSON_GetObjectItemCaseSensitive(root, "awarded");
    if (cJSON_IsBool(awarded)) result.ok = cJSON_IsTrue(awarded);
    if (cJSON_IsNumber(awarded)) result.ok = awarded->valueint != 0;

    cJSON* points = cJSON_GetObjectItemCaseSensitive(root, "points");
    if (cJSON_IsNumber(points) && points->valueint >= 0 && points->valueint <= 65535) {
        result.points = (uint16_t)points->valueint;
    }
    cJSON* onionsAwarded = cJSON_GetObjectItemCaseSensitive(root, "onionsAwarded");
    if (cJSON_IsNumber(onionsAwarded) && onionsAwarded->valueint >= 0 && onionsAwarded->valueint <= 65535) {
        result.points = (uint16_t)onionsAwarded->valueint;
    }

    cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (cJSON_IsString(message) && message->valuestring) {
        copyFixed(result.message, sizeof(result.message), message->valuestring);
    } else if (status == "checked_in") {
        copyFixed(result.message, sizeof(result.message), "Attendance recorded");
    } else if (status == "already_checked_in") {
        copyFixed(result.message, sizeof(result.message), "Already checked in");
    } else if (status == "no_current_workshop") {
        copyFixed(result.message, sizeof(result.message), "No active workshop");
    } else if (status == "badge_not_linked") {
        copyFixed(result.message, sizeof(result.message), "Badge not linked");
    } else if (status == "unknown_beacon") {
        copyFixed(result.message, sizeof(result.message), "Unknown beacon");
    }

    if (status.length()) {
        result.ok = status == "checked_in" && result.points > 0;
    }

    cJSON_Delete(root);
}

static String uint64ToString(uint64_t value) {
    char out[24];
    snprintf(out, sizeof(out), "%llu", (unsigned long long)value);
    return String(out);
}

static PostResult postApprove(const ApprovePacket& approve, const uint8_t sourceMac[6], int8_t receivedRssi) {
    PostResult result;
    setDefaultPostResult(result);

    if (isPlaceholder(BEACON_SERVER_URL)) {
        copyFixed(result.message, sizeof(result.message), "Server URL not configured");
        return result;
    }
    if (!connectWiFi(kHttpTimeoutMs)) {
        copyFixed(result.message, sizeof(result.message), "WiFi unavailable");
        return result;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "badge_check_in");
    cJSON_AddStringToObject(root, "beaconId", fixedString(approve.beaconId, sizeof(approve.beaconId)).c_str());
    cJSON_AddStringToObject(root, "configuredBeaconId", BEACON_ID);
    cJSON_AddStringToObject(root, "room", BEACON_ROOM);
    cJSON_AddStringToObject(root, "nonce", nonceToHex(approve.nonce).c_str());
    cJSON_AddStringToObject(root, "hardwareId", fixedString(approve.hardwareId, sizeof(approve.hardwareId)).c_str());
    cJSON_AddNumberToObject(root, "onionId", (double)approve.onionId);
    cJSON_AddStringToObject(root, "onionIdString", uint64ToString(approve.onionId).c_str());
    cJSON_AddStringToObject(root, "username", fixedString(approve.username, sizeof(approve.username)).c_str());
    cJSON_AddStringToObject(root, "wallet", fixedString(approve.wallet, sizeof(approve.wallet)).c_str());
    cJSON_AddNumberToObject(root, "badgeReportedRssi", approve.rssi);
    cJSON_AddNumberToObject(root, "beaconReceivedRssi", receivedRssi);
    cJSON_AddNumberToObject(root, "approvedAt", approve.approvedAt);
    cJSON_AddStringToObject(root, "badgeMac", macToString(approve.badgeMac).c_str());
    cJSON_AddStringToObject(root, "sourceMac", macToString(sourceMac).c_str());
    char* printed = cJSON_PrintUnformatted(root);
    String body = printed ? String(printed) : "{}";
    if (printed) cJSON_free(printed);
    cJSON_Delete(root);

    String response;
    esp_http_client_config_t cfg = {};
    cfg.url = BEACON_SERVER_URL;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.event_handler = httpCaptureEvent;
    cfg.user_data = &response;
    if (BEACON_SERVER_CA_PEM[0]) {
        cfg.cert_pem = BEACON_SERVER_CA_PEM;
    } else {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        copyFixed(result.message, sizeof(result.message), "HTTP setup failed");
        return result;
    }

    esp_http_client_set_method(http, HTTP_METHOD_POST);
    esp_http_client_set_header(http, "Content-Type", "application/json");
    if (!isPlaceholder(BEACON_SHARED_SECRET)) {
        String auth = String("Bearer ") + BEACON_SHARED_SECRET;
        esp_http_client_set_header(http, "Authorization", auth.c_str());
    }
    esp_http_client_set_post_field(http, body.c_str(), body.length());

    esp_err_t err = esp_http_client_perform(http);
    result.httpCode = err == ESP_OK ? esp_http_client_get_status_code(http) : -1;
    esp_http_client_cleanup(http);

    bool accepted = result.httpCode >= 200 && result.httpCode < 300;
    if (accepted) {
        result.points = BEACON_DEFAULT_POINTS;
        copyFixed(result.message, sizeof(result.message), "Checked in");
    } else {
        copyFixed(result.message, sizeof(result.message), "Server rejected check-in");
    }
    result.ok = accepted;
    applyServerResult(result, response);

    Serial.printf("[beacon] POST %s -> %d %s\n", BEACON_SERVER_URL, result.httpCode, response.c_str());
    return result;
}

static void sendResult(const uint8_t destMac[6], const ApprovePacket& approve, const PostResult& post) {
    ResultPacket result = {};
    initHeader(result.header, kTypeResult);
    copyFixed(result.beaconId, sizeof(result.beaconId), BEACON_ID);
    memcpy(result.nonce, approve.nonce, sizeof(result.nonce));
    result.awarded = post.ok ? 1 : 0;
    result.points = post.points;
    copyFixed(result.message, sizeof(result.message), post.message);

    sendPacket(destMac, &result, sizeof(result));
    Serial.printf("[beacon] result -> %s awarded=%u points=%u message=%s\n",
                  macToString(destMac).c_str(), result.awarded, result.points, result.message);
}

static void handleApprovePacket(const RxPacket& packet) {
    if (packet.len != sizeof(ApprovePacket)) {
        Serial.printf("[beacon] ignored packet len=%u from %s\n",
                      packet.len, macToString(packet.mac).c_str());
        return;
    }

    ApprovePacket approve;
    memcpy(&approve, packet.payload, sizeof(approve));
    if (!validHeader(approve.header, kTypeApprove)) {
        Serial.printf("[beacon] ignored non-approve packet from %s\n", macToString(packet.mac).c_str());
        return;
    }

    String packetBeaconId = fixedString(approve.beaconId, sizeof(approve.beaconId));
    if (packetBeaconId != String(BEACON_ID)) {
        Serial.printf("[beacon] ignored approve for beacon=%s\n", packetBeaconId.c_str());
        return;
    }
    if (!nonceMatches(approve.nonce)) {
        Serial.printf("[beacon] ignored approve with stale nonce=%s\n", nonceToHex(approve.nonce).c_str());
        return;
    }

    Serial.printf("[beacon] approve from %s user=%s onion=%llu badgeRssi=%d rxRssi=%d\n",
                  macToString(packet.mac).c_str(),
                  fixedString(approve.username, sizeof(approve.username)).c_str(),
                  (unsigned long long)approve.onionId,
                  approve.rssi,
                  packet.receivedRssi);

    PostResult post = postApprove(approve, packet.mac, packet.receivedRssi);
    sendResult(packet.mac, approve, post);
}

static void initNonce() {
    for (uint8_t i = 0; i < sizeof(s_nonce); i += 4) {
        uint32_t r = esp_random();
        size_t remaining = sizeof(s_nonce) - i;
        size_t copyLen = remaining < sizeof(r) ? remaining : sizeof(r);
        memcpy(s_nonce + i, &r, copyLen);
    }
    Serial.printf("[beacon] nonce=%s\n", nonceToHex(s_nonce).c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.printf("[beacon] boot id=%s room=%s label=%s\n", BEACON_ID, BEACON_ROOM, BEACON_LABEL);

    initNonce();
    connectWiFi(10000);
    ensureEspNow();
    advertise();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED && millis() - s_lastWifiAttempt > kWifiRetryMs) {
        s_lastWifiAttempt = millis();
        connectWiFi(1000);
        ensureEspNow();
    }

    if (millis() - s_lastAdvertise >= BEACON_ADVERTISE_INTERVAL_MS) {
        s_lastAdvertise = millis();
        advertise();
    }

    RxPacket packet;
    while (rxQueuePop(packet)) {
        handleApprovePacket(packet);
    }

    delay(5);
}
