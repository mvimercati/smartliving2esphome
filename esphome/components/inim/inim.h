#pragma once
// INIM SmartLiving 1050 on the ESP UART: polls the panel and publishes ESPHome entities;
// PIN commands come from API actions (Home Assistant template panels/locks).
//
// One transaction at a time and never blocking loop(): a request is sent, the answer is
// collected across loop() calls until it is complete or times out. The PIN of a command is
// kept only until the command has been sent, then wiped; it is never logged.

#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "protocol.h"

namespace esphome {
namespace inim {

// Known bits of the faults block, in the order of the YAML fault sensors (see __init__.py)
enum FaultGroup : uint8_t { GROUP_NONE, GROUP_POWER, GROUP_PHONE, GROUP_RADIO, GROUP_TAMPER };
struct FaultBit {
  uint8_t byte;
  uint8_t bit;
  FaultGroup group;
};
static constexpr FaultBit FAULT_BITS[] = {
    {0, 3, GROUP_POWER},   // fault_mains
    {0, 2, GROUP_POWER},   // fault_battery
    {0, 0, GROUP_POWER},   // fuse_zones
    {0, 1, GROUP_POWER},   // fuse_ibus
    {0, 4, GROUP_PHONE},   // fault_phone
    {4, 1, GROUP_PHONE},   // fault_gsm (GSM module, found on the installation; not in inim.java)
    {0, 5, GROUP_RADIO},   // fault_jamming
    {0, 6, GROUP_RADIO},   // fault_radio_battery
    {0, 7, GROUP_RADIO},   // fault_radio_loss
    {4, 4, GROUP_TAMPER},  // tamper_panel
    {4, 5, GROUP_TAMPER},  // tamper_cover
    {4, 6, GROUP_TAMPER},  // tamper_reader
    {4, 7, GROUP_TAMPER},  // tamper_keypad
    {5, 0, GROUP_TAMPER},  // tamper_expander
    {5, 1, GROUP_NONE},    // reset
    {5, 4, GROUP_NONE},    // internet
    {5, 5, GROUP_NONE},    // service
    {5, 6, GROUP_NONE},    // program
    {5, 7, GROUP_NONE},    // voice
};
static constexpr size_t NUM_FAULT_BITS = sizeof(FAULT_BITS) / sizeof(FAULT_BITS[0]);

enum Summary : uint8_t {
  SUMMARY_TAMPER,  // areas in tamper + panel tamper flags
  SUMMARY_ALARM_MEMORY,
  SUMMARY_TAMPER_MEMORY,
  SUMMARY_FAULT,  // any fault_/fuse_ flag
  SUMMARY_FAULT_POWER,
  SUMMARY_FAULT_PHONE,
  SUMMARY_FAULT_RADIO,
  NUM_SUMMARIES,
};

enum Scenario : uint8_t { SCENARIO_DISARMED, SCENARIO_ARMED_AWAY, SCENARIO_ARMED_HOME, SCENARIO_ARMED_NIGHT, NUM_SCENARIOS };

struct Zone {
  uint8_t id;        // 0-based zone number
  uint8_t terminal;  // physical terminal, addressed by the bypass command
  const char *name;
  binary_sensor::BinarySensor *state;   // on unless closed
  binary_sensor::BinarySensor *bypass;  // on = excluded
  uint8_t raw;  // last zone state read (0 short, 1 closed, 2 open, 3 tamper), 0xFF = none yet
};

struct Area {
  uint8_t id;
  const char *name;
  text_sensor::TextSensor *state;  // Home Assistant alarm state
  bool memory;                     // alarm or tamper memory
};

class InimPanel : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Installation (from YAML)
  void add_zone(uint8_t id, uint8_t terminal, const char *name, binary_sensor::BinarySensor *state,
                binary_sensor::BinarySensor *bypass) {
    this->zones_.push_back({id, terminal, name, state, bypass, 0xFF});
  }
  void add_area(uint8_t id, const char *name, text_sensor::TextSensor *state) {
    this->areas_.push_back({id, name, state, false});
  }
  void set_scenario(uint8_t scenario, const char *hex);  // 12 hex digits, as in the config

  // Entities
  void set_fault_sensor(uint8_t index, binary_sensor::BinarySensor *s) { this->fault_sensors_[index] = s; }
  void set_summary_sensor(uint8_t index, binary_sensor::BinarySensor *s) { this->summary_sensors_[index] = s; }
  void set_connection_sensor(binary_sensor::BinarySensor *s) { this->connection_sensor_ = s; }
  void set_bypassed_sensor(binary_sensor::BinarySensor *s) { this->bypassed_sensor_ = s; }
  void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }
  void set_psu_sensor(sensor::Sensor *s) { this->psu_sensor_ = s; }
  void set_alarm_state_sensor(text_sensor::TextSensor *s) { this->alarm_state_sensor_ = s; }
  void set_faults_summary_sensor(text_sensor::TextSensor *s) { this->faults_summary_sensor_ = s; }
  void set_last_write_sensor(text_sensor::TextSensor *s) { this->last_write_sensor_ = s; }
  void set_scenario_code_sensor(text_sensor::TextSensor *s) { this->scenario_code_sensor_ = s; }
  void set_last_event_sensor(text_sensor::TextSensor *s) { this->last_event_sensor_ = s; }
  void set_last_event_time_sensor(text_sensor::TextSensor *s) { this->last_event_time_sensor_ = s; }
  void set_zone_alarm_sensor(text_sensor::TextSensor *s) { this->zone_alarm_sensor_ = s; }

  // Timing
  void set_timeout(uint32_t ms) { this->timeout_ = ms; }
  void set_settle(uint32_t ms) { this->settle_ = ms; }
  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }
  void set_slow_poll_interval(uint32_t ms) { this->slow_poll_interval_ = ms; }
  void set_offline_after(uint32_t ms) { this->offline_after_ = ms; }
  void set_command_max_age(uint32_t ms) { this->command_max_age_ = ms; }
  void set_confirm_reads(uint8_t n) { this->confirm_reads_ = n; }
  void set_write_attempts(uint8_t n) { this->write_attempts_ = n; }
  void set_log_max_entries(uint8_t n) { this->log_max_entries_ = n; }
  void set_psu_voltage_threshold(float v) { this->psu_threshold_ = v; }

  // Commands (API actions). The result is published on the "Last write result" sensor.
  void arm(const std::string &scenario, const std::string &pin);           // disarmed/armed_away/armed_home/armed_night
  void arm_area(int area, const std::string &mode, const std::string &pin);  // same modes, one area
  void bypass_zone(int zone, bool exclude, const std::string &pin);
  void include_all(const std::string &pin);
  void reset_memory(const std::string &pin);
  void refresh();

 protected:
  enum class Op : uint8_t {
    NONE,
    ZONES,
    AREAS,
    FAULTS,
    BYPASS_TEST,
    MEMORY,
    LOG_HEAD,
    LOG_ENTRY,
    WRITE,
    WRITE_RESULT,
  };
  enum class CommandType : uint8_t { ARM, ARM_AREA, BYPASS, INCLUDE_ALL, RESET_MEMORY };
  struct Command {
    CommandType type;
    uint8_t target;  // area / zone index, scenario
    uint8_t value;   // area mode, bypass flag
    uint8_t pin[PIN_LEN];
    uint32_t received;
  };
  static constexpr uint8_t MAX_COMMANDS = 4;
  static constexpr uint8_t MAX_STEPS = NUM_ZONES;  // writes of one command (include all: one per zone)

  // Transactions
  struct Read {
    uint16_t address;
    uint16_t length;
  };
  Read read_of_(Op op) const;
  void start_(Op op);
  void receive_(uint32_t now);
  void check_answer_();
  bool confirmed_(size_t len);
  void finish_(const uint8_t *data);
  void fail_(const char *reason);
  Op next_op_(uint32_t now);
  void drain_();

  // Decoding + publishing
  void handle_zones_(const uint8_t *d);
  void handle_areas_(const uint8_t *d);
  void handle_faults_(const uint8_t *d);
  void handle_bypass_test_(const uint8_t *d);
  void handle_memory_(const uint8_t *d);
  void handle_log_head_(const uint8_t *d);
  void handle_log_entry_(const uint8_t *d);
  void publish_alarm_state_(const uint8_t *d);
  void publish_tamper_();
  void update_connection_(uint32_t now);
  bool log_pending_() const { return this->log_head_ >= 0 && this->log_target_ >= 0 && this->log_head_ != this->log_target_; }
  void sequence_done_(uint32_t now);

  // Commands
  bool queue_command_(CommandType type, uint8_t target, uint8_t value, const std::string &pin);
  bool begin_command_(uint32_t now);
  void build_write_(uint8_t step);
  void write_done_(int result);
  void end_command_();
  void publish_result_(const std::string &text);

  // Installation
  std::vector<Zone> zones_;
  std::vector<Area> areas_;
  uint8_t scenarios_[NUM_SCENARIOS][AREA_BLOCK_LEN]{};
  bool has_scenario_[NUM_SCENARIOS]{};

  // Entities
  binary_sensor::BinarySensor *fault_sensors_[NUM_FAULT_BITS]{};
  binary_sensor::BinarySensor *summary_sensors_[NUM_SUMMARIES]{};
  binary_sensor::BinarySensor *connection_sensor_{nullptr};
  binary_sensor::BinarySensor *bypassed_sensor_{nullptr};
  sensor::Sensor *battery_sensor_{nullptr};
  sensor::Sensor *psu_sensor_{nullptr};
  text_sensor::TextSensor *alarm_state_sensor_{nullptr};
  text_sensor::TextSensor *faults_summary_sensor_{nullptr};
  text_sensor::TextSensor *last_write_sensor_{nullptr};
  text_sensor::TextSensor *scenario_code_sensor_{nullptr};
  text_sensor::TextSensor *last_event_sensor_{nullptr};
  text_sensor::TextSensor *last_event_time_sensor_{nullptr};
  text_sensor::TextSensor *zone_alarm_sensor_{nullptr};

  // Timing
  uint32_t timeout_{1000};
  uint32_t settle_{500};
  uint32_t poll_interval_{1000};
  uint32_t slow_poll_interval_{5000};
  uint32_t offline_after_{30000};
  uint32_t command_max_age_{20000};
  uint8_t confirm_reads_{3};
  uint8_t write_attempts_{2};
  uint8_t log_max_entries_{20};
  float psu_threshold_{13.0f};

  // Transaction in flight
  Op op_{Op::NONE};
  uint8_t request_[HEADER_LEN + MAX_WRITE_PAYLOAD]{};
  uint8_t rx_[LOG_ENTRY_RESP_LEN]{};
  size_t rx_len_{0};
  size_t rx_expected_{0};
  uint32_t sent_at_{0};
  uint8_t first_[LEN_ZONE_MEMORY_OUTPUTS]{};  // first answer of a confirmed read
  bool second_read_{false};
  uint8_t confirm_left_{0};  // reads still to be done twice (after a failure or at boot)
  uint32_t quiet_until_{0};  // no request before this (late bytes may still arrive)
  uint8_t errors_{0};        // consecutive failed transactions

  // Poll sequences
  static constexpr Op POLL_OPS[] = {Op::ZONES, Op::AREAS, Op::FAULTS};
  static constexpr Op SLOW_OPS[] = {Op::BYPASS_TEST, Op::MEMORY, Op::LOG_HEAD};
  const Op *seq_{nullptr};  // sequence in progress, nullptr = none
  uint8_t seq_len_{0};
  uint8_t seq_pos_{0};
  uint32_t next_poll_{0};
  uint32_t next_slow_poll_{0};
  uint32_t last_ok_{0};
  bool ever_ok_{false};
  bool force_publish_{false};  // republish text/numeric states at the next reads

  // State shared between reads
  bool area_tamper_{false};
  bool panel_tamper_{false};
  int16_t volts_tenths_{-1};
  int16_t log_head_{-1};  // next entry to read, -1 = resync without replaying
  int16_t log_target_{-1};

  // Commands
  Command queue_[MAX_COMMANDS]{};
  uint8_t queue_head_{0};
  uint8_t queue_count_{0};
  Command cmd_{};
  bool cmd_active_{false};
  uint8_t steps_[MAX_STEPS]{};  // zone/area index per write
  uint8_t step_count_{0};
  uint8_t step_{0};
  uint8_t attempt_{0};
  uint8_t write_payload_len_{0};
  bool await_result_{false};  // write sent, result still to be read
};

class RefreshButton : public button::Button, public Parented<InimPanel> {
 protected:
  void press_action() override { this->parent_->refresh(); }
};

}  // namespace inim
}  // namespace esphome
