// Host test of the pure protocol helpers (protocol.h) against frames and decodings checked on the
// panel. From the repository root (needs g++, see README.md):
//   g++ -std=c++17 -Wall -Iesphome/components/inim esphome/tests/test_protocol.cpp -o test_protocol && ./test_protocol
#include <cstdio>
#include <initializer_list>
#include <string>

#include "protocol.h"

using namespace esphome::inim;

static int failures = 0;

static void expect(const char *label, const std::string &actual, const char *expected) {
  if (actual == expected)
    return;
  printf("FAIL %s\n  expected: %s\n  actual:   %s\n", label, expected, actual.c_str());
  failures++;
}

static std::string hex(const uint8_t *data, size_t len) {
  std::string out;
  char byte[4];
  for (size_t i = 0; i < len; i++) {
    snprintf(byte, sizeof(byte), i ? " %02x" : "%02x", data[i]);
    out += byte;
  }
  return out;
}

static void test_frames() {
  uint8_t header[HEADER_LEN];
  make_header(header, OP_READ, ADDR_ZONE_STATUS, LEN_ZONE_STATUS);
  expect("read zones", hex(header, HEADER_LEN), "00 00 00 20 01 00 0d 2e");
  make_header(header, OP_READ, ADDR_LOG_ENTRY, 499);  // log entry index in the length field
  expect("read log entry 499", hex(header, HEADER_LEN), "00 00 00 1f ff 01 f3 12");
}

static void test_pin() {
  uint8_t pin[PIN_LEN];
  expect("pin accepted", std::to_string(encode_pin("1234", pin)), "1");
  expect("pin digits", hex(pin, PIN_LEN), "01 02 03 04 ff ff");
  for (const char *bad : {"", "1234567", "12a4"})
    expect("pin rejected", std::to_string(encode_pin(bad, pin)), "0");
}

static void test_zones() {
  const uint8_t zones[LEN_ZONE_STATUS] = {0x55, 0x56, 0x5B, 0xE5, 0x55, 0x55, 0x55,
                                          0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
  std::string states;
  for (uint8_t z = 0; z < NUM_ZONES; z++)
    states += (z ? " " : "") + std::to_string(zone_state(zones, z));
  expect("zone states", states,
         "1 1 1 1 2 1 1 1 3 2 1 1 1 1 2 3 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1");
}

static void test_areas() {
  const uint8_t areas[LEN_AREA_STATUS] = {0x31, 0x24, 0x44, 0x44, 0x44, 0x00, 0x02, 0x00,
                                          0x01, 0x00, 0x82, 0x01, 0x00, 0x00, 0x00, 0x00};
  std::string states;
  for (uint8_t a = 0; a < NUM_AREAS; a++) {
    // arm mode / alarm, tamper, alarm memory, tamper memory
    states += (a ? " " : "") + std::to_string(area_armed(areas, a)) + "/" +
              std::to_string(area_flag(areas, AREA_BIT_ALARM, a)) + std::to_string(area_flag(areas, AREA_BIT_TAMPER, a)) +
              std::to_string(area_flag(areas, AREA_BIT_ALARM_MEM, a)) +
              std::to_string(area_flag(areas, AREA_BIT_TAMPER_MEM, a));
  }
  expect("area states", states, "1/0100 3/1010 4/0000 2/0000 4/0000 4/0000 4/0000 4/0010 4/0010 4/0000");
}

static void test_faults() {
  const uint8_t faults[LEN_FAULTS_BATT] = {0x00, 0x01, 0x84, 0x03, 0x0A, 0x08};
  char text[32];
  snprintf(text, sizeof(text), "0x%X %.1f", unsigned(faults_summary(faults)), battery_voltage(faults));
  expect("faults summary and battery", text, "0x10A00 13.6");
}

static void test_dates() {
  // days since 2000-01-01 (panel clock) -> civil date
  const struct {
    int32_t days;
    const char *date;
  } cases[] = {{0, "2000-01-01"},   {59, "2000-02-29"},   {60, "2000-03-01"},
               {365, "2000-12-31"}, {9785, "2026-10-16"}, {9786, "2026-10-17"}};
  for (const auto &c : cases) {
    int y, m, d;
    civil_from_days(c.days, y, m, d);
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    expect("date", date, c.date);
  }
}

int main() {
  test_frames();
  test_pin();
  test_zones();
  test_areas();
  test_faults();
  test_dates();
  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
