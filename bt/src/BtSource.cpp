#include "BtSource.h"

#include <Arduino.h>

#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace drehklang::bt {

namespace {
BtSource *g_self = nullptr;
// 8 x 1.28 s: the 10 s the spec gives a scan.
constexpr uint8_t kInquiryLength = 8;

void toAddress(const uint8_t *bda, btlink::Address &out) { std::memcpy(out.data(), bda, 6); }

// The name from a discovery result: the full or short EIR name, then the
// BDNAME property, then the address. `out` has room for kMaxNameBytes + 1;
// names are cut at a character boundary (btlink::copyName).
void nameOf(esp_bt_gap_cb_param_t *param, char *out, size_t size) {
  out[0] = '\0';
  for (int i = 0; i < param->disc_res.num_prop; ++i) {
    esp_bt_gap_dev_prop_t &prop = param->disc_res.prop[i];
    if (prop.type == ESP_BT_GAP_DEV_PROP_EIR) {
      uint8_t length = 0;
      auto *eir = static_cast<uint8_t *>(prop.val);
      uint8_t *name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
      if (name == nullptr) {
        name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
      }
      if (name != nullptr) {
        btlink::copyName(name, length, out);
        return;
      }
    } else if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME) {
      const auto *raw = static_cast<const char *>(prop.val);
      btlink::copyName(reinterpret_cast<const uint8_t *>(raw), strnlen(raw, prop.len), out);
      return;
    }
  }
  const uint8_t *a = param->disc_res.bda;
  snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}
}  // namespace

// Keeps Arduino from releasing the Classic BT controller's memory before
// setup() (esp32-hal-misc.c). BLE is not used; its memory is released.
extern "C" bool btClassicInUse() { return true; }

BtSource::BtSource(btaudio::HeadphoneStream &stream) : stream_(stream) { g_self = this; }

void BtSource::begin() {
  events_ = xQueueCreate(16, sizeof(BtSourceEvent));
  esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  controller.mode = ESP_BT_MODE_CLASSIC_BT;
  esp_bt_controller_init(&controller);
  esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
  esp_bluedroid_config_t stack = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  esp_bluedroid_init_with_cfg(&stack);
  esp_bluedroid_enable();

  esp_bt_gap_set_device_name("Drehklang");
  esp_bt_gap_register_callback(
      [](esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p) { onGap(e, p); });
  // Headphones confirm nothing on a screen: "just works" pairing.
  esp_bt_io_cap_t ioCap = ESP_BT_IO_CAP_NONE;
  esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &ioCap, sizeof(ioCap));

  // AVRCP before A2DP, as ESP-IDF asks.
  esp_avrc_ct_init();
  esp_avrc_tg_register_callback(
      [](esp_avrc_tg_cb_event_t e, esp_avrc_tg_cb_param_t *p) { onAvrcTarget(e, p); });
  esp_avrc_tg_init();
  esp_avrc_psth_bit_mask_t commands = {};
  esp_avrc_tg_get_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_ALLOWED_CMD, &commands);
  esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &commands);

  esp_a2d_register_callback([](esp_a2d_cb_event_t e, esp_a2d_cb_param_t *p) { onA2dp(e, p); });
  esp_a2d_source_register_data_callback(&BtSource::onData);
  esp_a2d_source_init();

  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

bool BtSource::nextEvent(BtSourceEvent &out) {
  return xQueueReceive(events_, &out, 0) == pdTRUE;
}

bool BtSource::startScan() {
  // Scanning from here on, not from DISC_STATE_CHANGED: the STATE sent right
  // after SCAN_START must not already say the scan is over.
  if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquiryLength, 0) != ESP_OK) {
    return false;
  }
  scanning_.store(true);
  return true;
}

void BtSource::stopScan() { esp_bt_gap_cancel_discovery(); }

bool BtSource::connect(const btlink::Address &address) {
  peer_ = address;
  connectingSince_.store(millis());
  link_.store(btlink::LinkState::Connecting);
  const esp_err_t result = esp_a2d_source_connect(peer_.data());
  if (result == ESP_OK) return true;
  link_.store(btlink::LinkState::Idle);
  log("connect refused: %s", esp_err_to_name(result));
  return false;
}

void BtSource::abandonConnect() {
  if (link_.load() != btlink::LinkState::Connecting) return;
  log("connect never answered, giving up");
  link_.store(btlink::LinkState::Idle);
}

void BtSource::disconnect() {
  if (link_.load() == btlink::LinkState::Idle) return;
  btlink::Address copy = peer_;
  esp_a2d_source_disconnect(copy.data());
}

void BtSource::removeBond(const btlink::Address &address) {
  btlink::Address copy = address;
  esp_bt_gap_remove_bond_device(copy.data());
}

void BtSource::startMedia() { esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY); }

void BtSource::post(const BtSourceEvent &event) { xQueueSend(events_, &event, 0); }

void BtSource::log(const char *format, ...) {
  BtSourceEvent line{BtSourceEvent::Kind::Log};
  va_list args;
  va_start(args, format);
  vsnprintf(line.text, sizeof(line.text), format, args);
  va_end(args);
  g_self->post(line);
}

void BtSource::onGap(int event, void *raw) {
  auto *param = static_cast<esp_bt_gap_cb_param_t *>(raw);
  switch (static_cast<esp_bt_gap_cb_event_t>(event)) {
    case ESP_BT_GAP_DISC_RES_EVT: {
      uint32_t cod = 0;
      int8_t rssi = -127;
      for (int i = 0; i < param->disc_res.num_prop; ++i) {
        const auto &prop = param->disc_res.prop[i];
        if (prop.type == ESP_BT_GAP_DEV_PROP_COD) cod = *static_cast<uint32_t *>(prop.val);
        if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI) rssi = *static_cast<int8_t *>(prop.val);
      }
      // Headphones and speakers only: the list is for something to listen with.
      if (!esp_bt_gap_is_valid_cod(cod) ||
          esp_bt_gap_get_cod_major_dev(cod) != ESP_BT_COD_MAJOR_DEV_AV) {
        return;
      }
      BtSourceEvent found{BtSourceEvent::Kind::Found};
      toAddress(param->disc_res.bda, found.address);
      found.rssi = rssi;
      nameOf(param, found.name, sizeof(found.name));
      g_self->post(found);
      break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      g_self->scanning_.store(param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED);
      break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      log("auth %s: status %d", reinterpret_cast<const char *>(param->auth_cmpl.device_name),
          static_cast<int>(param->auth_cmpl.stat));
      break;
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
      log("acl connected: status %d", static_cast<int>(param->acl_conn_cmpl_stat.stat));
      break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
      log("acl disconnected: reason 0x%02x", static_cast<int>(param->acl_disconn_cmpl_stat.reason));
      break;
    case ESP_BT_GAP_KEY_REQ_EVT:
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      log("pairing asks for a passkey (event %d)", event);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
      log("pin requested");
      // Legacy pairing: the PIN headphones nearly always use.
      esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
      esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
      break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
      log("ssp confirm %lu", static_cast<unsigned long>(param->cfm_req.num_val));
      esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
      break;
    default:
      break;
  }
}

void BtSource::onA2dp(int event, void *raw) {
  auto *param = static_cast<esp_a2d_cb_param_t *>(raw);
  switch (static_cast<esp_a2d_cb_event_t>(event)) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
      const auto state = param->conn_stat.state;
      log("a2dp state %d, reason %d", static_cast<int>(state),
          static_cast<int>(param->conn_stat.disc_rsn));
      if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        toAddress(param->conn_stat.remote_bda, g_self->peer_);
        g_self->link_.store(btlink::LinkState::Connected);
        BtSourceEvent connected{BtSourceEvent::Kind::Connected};
        g_self->post(connected);
      } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        g_self->link_.store(btlink::LinkState::Idle);
        BtSourceEvent gone{BtSourceEvent::Kind::Disconnected};
        g_self->post(gone);
      } else if (state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
        if (g_self->link_.load() != btlink::LinkState::Connecting) {
          g_self->connectingSince_.store(millis());
        }
        g_self->link_.store(btlink::LinkState::Connecting);
      }
      break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
      log("a2dp audio state %d", static_cast<int>(param->audio_stat.state));
      break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
      log("media ctrl %d ack %d", static_cast<int>(param->media_ctrl_stat.cmd),
          static_cast<int>(param->media_ctrl_stat.status));
      // The stream stays started while connected: some headphones switch
      // themselves off after a few minutes of a suspended stream.
      if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
          param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
      }
      break;
    default:
      break;
  }
}

void BtSource::onAvrcTarget(int event, void *raw) {
  auto *param = static_cast<esp_avrc_tg_cb_param_t *>(raw);
  if (static_cast<esp_avrc_tg_cb_event_t>(event) != ESP_AVRC_TG_PASSTHROUGH_CMD_EVT) return;
  if (param->psth_cmd.key_state != ESP_AVRC_PT_CMD_STATE_PRESSED) return;
  BtSourceEvent pressed{BtSourceEvent::Kind::Button};
  switch (param->psth_cmd.key_code) {
    case ESP_AVRC_PT_CMD_PLAY:
      pressed.button = btlink::ButtonCode::Play;
      break;
    case ESP_AVRC_PT_CMD_PAUSE:
    case ESP_AVRC_PT_CMD_STOP:
      pressed.button = btlink::ButtonCode::Pause;
      break;
    default:
      return;
  }
  g_self->post(pressed);
}

// Bluedroid asks for `length` bytes of 44.1 kHz 16-bit stereo PCM.
int32_t BtSource::onData(uint8_t *data, int32_t length) {
  if (data == nullptr || length <= 0) return 0;
  const size_t frames = static_cast<size_t>(length) / 4;
  g_self->stream_.render(reinterpret_cast<int16_t *>(data), frames);
  return static_cast<int32_t>(frames * 4);
}

}  // namespace drehklang::bt
