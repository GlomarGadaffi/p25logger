#ifndef CC_LOGGER_H
#define CC_LOGGER_H

#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <chrono>
#include <json.hpp>

#include "systems/parser.h"
#include "talkgroups.h"

using json = nlohmann::ordered_json;

/**
 * P25 Control Channel Logger
 *
 * Converts every decoded TrunkMessage into structured NDJSON and writes
 * to stdout. Maintains minimal state for decode rate monitoring.
 */
class CCLogger {
public:
  CCLogger();

  // Set the talkgroup lookup table for enrichment
  void set_talkgroups(Talkgroups *tgs);

  // Set the system short name for log output
  void set_short_name(const std::string &name);

  // Convert a TrunkMessage to JSON and write to stdout
  void log_message(const TrunkMessage &msg);

  // Log decode rate statistics
  void log_decode_rate(int msgs_per_sec, double control_freq);

  // Log a system event (retune, error, etc.)
  void log_event(const std::string &event_type, const std::string &detail);

private:
  Talkgroups *talkgroups_;
  std::string short_name_;

  // Get current ISO 8601 timestamp with milliseconds
  std::string now_iso8601();

  // Get the human-readable name for a message type
  static const char *message_type_name(MessageType type);

  // Get the human-readable name for a TSBK/MBT opcode
  static const char *opcode_name(unsigned long opcode);

  // Look up talkgroup alpha tag
  std::string lookup_tg_tag(long talkgroup);

  // Format a hex value as "0xABCD"
  static std::string to_hex(unsigned long val, int width = 0);
};

#endif // CC_LOGGER_H
