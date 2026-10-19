#pragma once
// INIM SmartLiving 1050 serial protocol: frame encoding and decoding.
// Pure functions, no I/O and no ESPHome dependency (tested on the host: tests/test_protocol.cpp).
//
// Every request starts with an 8-byte header:
//   [op, 0x00, 0x00, addr_hi, addr_lo, len_hi, len_lo, checksum]
// op is 0x00 for a read and 0x01 for a write; checksum = sum of the previous 7 bytes & 0xFF.
// * Read: the panel answers `len` data bytes followed by a checksum byte.
// * Write: the header is followed by `len` payload bytes; the panel answers one byte equal
//   to the checksum of the payload, then the outcome is read at ADDR_WRITE_RESULT.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace inim {

static constexpr uint8_t OP_READ = 0x00;
static constexpr uint8_t OP_WRITE = 0x01;
static constexpr size_t HEADER_LEN = 8;

// Read addresses / data lengths
static constexpr uint16_t ADDR_AREA_STATUS = 0x2000;
static constexpr uint16_t LEN_AREA_STATUS = 16;
static constexpr uint16_t ADDR_ZONE_STATUS = 0x2001;
// The block is 26 bytes but only the first 13 hold the 1050's 50 zones: reading less makes
// the answer (14 bytes) differ from the bypass/test one (27), so a late answer can't pass for
// another. Keep every answer length unique.
static constexpr uint16_t LEN_ZONE_STATUS = 13;
static constexpr uint16_t ADDR_ZONE_BYPASS_TEST = 0x2002;
static constexpr uint16_t LEN_ZONE_BYPASS_TEST = 26;
static constexpr uint16_t ADDR_ZONE_MEMORY_OUTPUTS = 0x2003;
static constexpr uint16_t LEN_ZONE_MEMORY_OUTPUTS = 28;
static constexpr uint16_t ADDR_WRITE_RESULT = 0x2004;
static constexpr uint16_t LEN_WRITE_RESULT = 1;
static constexpr uint16_t ADDR_FAULTS_BATT = 0x2005;
static constexpr uint16_t LEN_FAULTS_BATT = 6;
// Bits compared when a read is confirmed: the battery voltage (byte 2, byte 3 bits 0-1)
// often changes by one step between two consecutive reads.
static constexpr uint8_t FAULTS_BATT_STABLE_MASK[LEN_FAULTS_BATT] = {0xFF, 0xFF, 0x00, 0xFC, 0xFF, 0xFF};

// Event log: the head index is little endian; an entry is read with its index in the
// length field of the header and answers 366 data bytes + checksum.
static constexpr uint16_t ADDR_LOG_HEAD = 0x1FFE;
static constexpr uint16_t LEN_LOG_HEAD = 4;
static constexpr uint16_t ADDR_LOG_ENTRY = 0x1FFF;
static constexpr uint16_t LOG_ENTRY_RESP_LEN = 367;
static constexpr uint16_t LOG_SIZE = 500;

// Writes
static constexpr uint16_t ADDR_ARM_AREAS = 0x2006;    // PIN + 8 bytes of area nibbles (0 = unchanged)
static constexpr uint16_t ADDR_RESET_AREAS = 0x2008;  // PIN + 2 bytes area bitmap
static constexpr uint16_t ADDR_BYPASS_ZONE = 0x2009;  // PIN + terminal + double zone flag + value (1 = bypass)
static constexpr size_t PIN_LEN = 6;
static constexpr size_t LEN_ARM_AREAS = PIN_LEN + 8;
static constexpr size_t MAX_WRITE_PAYLOAD = LEN_ARM_AREAS;

static constexpr uint8_t NUM_AREAS = 10;
static constexpr uint8_t NUM_ZONES = 50;
static constexpr size_t AREA_BLOCK_LEN = 6;  // raw area arm-state block, matched against scenarios

// Area arm nibbles
static constexpr uint8_t AREA_ARM_AWAY = 1;     // "Inserita", with entry/exit delays
static constexpr uint8_t AREA_ARM_STAY = 2;     // "Parziale"
static constexpr uint8_t AREA_ARM_INSTANT = 3;  // "Immediata", no delay
static constexpr uint8_t AREA_DISARM = 4;

inline uint8_t checksum(const uint8_t *data, size_t len) {
  uint8_t sum = 0;
  while (len--)
    sum += *data++;
  return sum;
}

inline void make_header(uint8_t *out, uint8_t op, uint16_t address, uint16_t length) {
  out[0] = op;
  out[1] = 0;
  out[2] = 0;
  out[3] = address >> 8;
  out[4] = address & 0xFF;
  out[5] = length >> 8;
  out[6] = length & 0xFF;
  out[7] = checksum(out, 7);
}

// PIN as one digit per byte, padded with 0xFF. Returns false unless 1-6 digits.
inline bool encode_pin(const char *pin, uint8_t *out) {
  size_t len = strlen(pin);
  if (len < 1 || len > PIN_LEN)
    return false;
  for (size_t i = 0; i < PIN_LEN; i++) {
    if (i < len) {
      if (pin[i] < '0' || pin[i] > '9')
        return false;
      out[i] = pin[i] - '0';
    } else {
      out[i] = 0xFF;
    }
  }
  return true;
}

// Bitmap, LSB first: bit n of byte i is item i*8+n.
inline bool bit_at(const uint8_t *data, size_t index) { return (data[index / 8] >> (index % 8)) & 1; }

// Zone state, 2 bits per zone: 0 short, 1 closed, 2 open, 3 tamper.
inline uint8_t zone_state(const uint8_t *data, uint8_t zone) { return (data[zone / 4] >> ((zone % 4) * 2)) & 0x3; }

// Area status block: arm nibble (low nibble = even area), then 2-byte bitmaps.
inline uint8_t area_armed(const uint8_t *data, uint8_t area) { return (data[area / 2] >> ((area % 2) * 4)) & 0xF; }
static constexpr uint8_t AREA_BIT_ALARM = 6;
static constexpr uint8_t AREA_BIT_TAMPER = 8;
static constexpr uint8_t AREA_BIT_ALARM_MEM = 10;
static constexpr uint8_t AREA_BIT_TAMPER_MEM = 12;
inline bool area_flag(const uint8_t *data, uint8_t base, uint8_t area) {
  return (data[base + area / 8] >> (area % 8)) & 1;
}

// Zone bypass/test block: 13 bytes bypass bitmap (1 = included), then the test bitmap.
inline bool zone_included(const uint8_t *data, uint8_t zone) { return bit_at(data, zone); }

// Zone memory block: 13 bytes alarm memory, then tamper memory and outputs.
inline bool zone_alarm_mem(const uint8_t *data, uint8_t zone) { return bit_at(data, zone); }

inline float battery_voltage(const uint8_t *faults) { return (((faults[3] & 0x3) << 8) | faults[2]) * 0.01516f; }

// Faults summary as shown since the Node.js version (byte 5 bit 3 = "disarmed", not a fault).
inline uint32_t faults_summary(const uint8_t *f) {
  return (uint32_t(f[0]) << 24) | (uint32_t(f[1]) << 16) | (uint32_t(f[4]) << 8) | (f[5] & 0xF7);
}

// Days since 2000-01-01 -> civil date (Howard Hinnant's civil_from_days).
inline void civil_from_days(int32_t days, int &year, int &month, int &day) {
  int32_t z = days + 10957 + 719468;  // 1970-01-01 -> 2000-01-01, then shift to 0000-03-01
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = uint32_t(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = int32_t(yoe) + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  day = int(doy - (153 * mp + 2) / 5 + 1);
  month = int(mp < 10 ? mp + 3 : mp - 9);
  year = int(y + (month <= 2));
}

}  // namespace inim
}  // namespace esphome
