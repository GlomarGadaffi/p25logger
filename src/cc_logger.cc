#include "cc_logger.h"
#include "formatter.h"
#include <iostream>
#include <iomanip>
#include <sstream>

CCLogger::CCLogger() : talkgroups_(nullptr) {}

void CCLogger::set_talkgroups(Talkgroups *tgs) {
  talkgroups_ = tgs;
}

void CCLogger::set_short_name(const std::string &name) {
  short_name_ = name;
}

std::string CCLogger::now_iso8601() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
  auto time = std::chrono::system_clock::to_time_t(now);
  struct tm tm_buf;
  gmtime_r(&time, &tm_buf);

  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);

  std::ostringstream oss;
  oss << buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
  return oss.str();
}

std::string CCLogger::to_hex(unsigned long val, int width) {
  std::ostringstream oss;
  oss << "0x";
  if (width > 0) {
    oss << std::setfill('0') << std::setw(width);
  }
  oss << std::uppercase << std::hex << val;
  return oss.str();
}

std::string CCLogger::lookup_tg_tag(long talkgroup) {
  if (talkgroups_ == nullptr || talkgroup <= 0) return "";
  Talkgroup *tg = talkgroups_->find_talkgroup(talkgroup);
  if (tg != nullptr) {
    return tg->alpha_tag;
  }
  return "";
}

const char *CCLogger::message_type_name(MessageType type) {
  switch (type) {
  case GRANT:             return "GRP_V_CH_GRANT";
  case STATUS:            return "NET_STS_BCST";
  case UPDATE:            return "GRP_V_CH_GRANT_UPDT";
  case CONTROL_CHANNEL:   return "CC_BCST";
  case REGISTRATION:      return "UNIT_REG_RSP";
  case DEREGISTRATION:    return "UNIT_DEREG_ACK";
  case AFFILIATION:       return "GRP_AFF_RSP";
  case SYSID:             return "RFSS_STS_BCST";
  case ACKNOWLEDGE:       return "ACK_RSP";
  case LOCATION:          return "LOC_REG_RSP";
  case PATCH_ADD:         return "PATCH_ADD";
  case PATCH_DELETE:      return "PATCH_DELETE";
  case DATA_GRANT:        return "SNDCP_DATA_CH_GRANT";
  case UU_ANS_REQ:        return "UU_ANS_REQ";
  case UU_V_GRANT:        return "UU_V_CH_GRANT";
  case UU_V_UPDATE:       return "UU_V_CH_GRANT_UPDT";
  case INVALID_CC_MESSAGE:return "INVALID_CC_MSG";
  case TDULC:             return "TDULC";
  case UNKNOWN:           return "UNKNOWN";
  default:                return "UNKNOWN";
  }
}

const char *CCLogger::opcode_name(unsigned long opcode) {
  switch (opcode) {
  case 0x00: return "GRP_V_CH_GRANT";
  case 0x02: return "GRP_V_CH_GRANT_UPDT";
  case 0x03: return "GRP_V_CH_GRANT_UPDT_EXP";
  case 0x04: return "UU_V_CH_GRANT";
  case 0x05: return "UU_ANS_REQ";
  case 0x06: return "UU_V_CH_GRANT_UPDT";
  case 0x08: return "TELE_INT_CH_GRANT";
  case 0x09: return "TELE_INT_CH_GRANT_UPDT";
  case 0x0a: return "TELE_INT_ANS_REQ";
  case 0x14: return "SNDCP_DATA_CH_GRANT";
  case 0x15: return "SNDCP_DATA_PAGE_REQ";
  case 0x16: return "SNDCP_DATA_CH_ANN";
  case 0x18: return "STS_UPDT";
  case 0x1a: return "STS_QUERY";
  case 0x1c: return "MSG_UPDT";
  case 0x1d: return "RADIO_UNIT_MON_CMD";
  case 0x1f: return "CALL_ALERT";
  case 0x20: return "ACK_RSP";
  case 0x21: return "EXT_FNCT_CMD";
  case 0x24: return "EXT_FNCT_CMD";
  case 0x27: return "DENY_RSP";
  case 0x28: return "GRP_AFF_RSP";
  case 0x29: return "SEC_CC_BCST_EXP";
  case 0x2a: return "GRP_AFF_QUERY";
  case 0x2b: return "LOC_REG_RSP";
  case 0x2c: return "UNIT_REG_RSP";
  case 0x2d: return "AUTH_CMD";
  case 0x2e: return "DEREG_ACK";
  case 0x2f: return "UNIT_DEREG_ACK";
  case 0x30: return "TDMA_SYNC_BCST";
  case 0x31: return "AUTH_DEMAND";
  case 0x32: return "AUTH_RSP";
  case 0x33: return "IDEN_UP_TDMA";
  case 0x34: return "IDEN_UP_VU";
  case 0x35: return "TIME_DATE_ANN";
  case 0x36: return "ROAM_ADDR_CMD";
  case 0x37: return "ROAM_ADDR_UPDT";
  case 0x38: return "SYS_SVC_BCST";
  case 0x39: return "SEC_CC_BCST";
  case 0x3a: return "RFSS_STS_BCST";
  case 0x3b: return "NET_STS_BCST";
  case 0x3c: return "ADJ_STS_BCST";
  case 0x3d: return "IDEN_UP";
  default:   return "UNKNOWN";
  }
}

void CCLogger::log_message(const TrunkMessage &msg) {
  // Skip truly empty / invalid messages that carry no information
  if (msg.message_type == UNKNOWN && msg.opcode == 255) {
    return;
  }

  json j;
  j["ts"] = now_iso8601();
  j["sys"] = short_name_;
  j["nac"] = to_hex(msg.nac);
  j["type"] = message_type_name(msg.message_type);

  // Include raw opcode for all messages
  if (msg.opcode != 255) {
    j["opcode"] = to_hex(msg.opcode, 2);
    j["opcode_name"] = opcode_name(msg.opcode);
  }

  // Source ID (unit ID)
  if (msg.source > 0) {
    j["source"] = msg.source;
  }

  // Talkgroup
  if (msg.talkgroup > 0) {
    j["talkgroup"] = msg.talkgroup;

    // Enrich with alpha tag from talkgroups.csv
    std::string tag = lookup_tg_tag(msg.talkgroup);
    if (!tag.empty()) {
      j["talkgroup_tag"] = tag;
    }
  }

  // Frequency
  if (msg.freq > 0) {
    // Output in MHz with 4 decimal places
    double freq_mhz = msg.freq / 1e6;
    j["freq"] = freq_mhz;
  }

  // Voice grant / call fields
  if (msg.message_type == GRANT || msg.message_type == UPDATE ||
      msg.message_type == UU_V_GRANT || msg.message_type == UU_V_UPDATE ||
      msg.message_type == DATA_GRANT) {
    j["emergency"] = msg.emergency;
    j["encrypted"] = msg.encrypted;

    if (msg.message_type == GRANT || msg.message_type == UU_V_GRANT ||
        msg.message_type == DATA_GRANT) {
      j["duplex"] = msg.duplex;
      j["mode"] = msg.mode;
      j["priority"] = msg.priority;
    }

    j["tdma"] = msg.phase2_tdma;
    if (msg.phase2_tdma) {
      j["tdma_slot"] = msg.tdma_slot;
    }
  }

  // System identification
  if (msg.message_type == SYSID) {
    if (msg.sys_id > 0)      j["sys_id"] = to_hex(msg.sys_id);
    if (msg.sys_rfss > 0)    j["rfss"] = msg.sys_rfss;
    if (msg.sys_site_id > 0) j["site_id"] = msg.sys_site_id;
  }

  // Network status
  if (msg.message_type == STATUS) {
    if (msg.wacn > 0)   j["wacn"] = to_hex(msg.wacn);
    if (msg.sys_id > 0) j["sys_id"] = to_hex(msg.sys_id);
  }

  // Patch data
  if (msg.message_type == PATCH_ADD || msg.message_type == PATCH_DELETE) {
    json patch;
    patch["supergroup"] = msg.patch_data.sg;
    if (msg.patch_data.ga1 > 0) patch["ga1"] = msg.patch_data.ga1;
    if (msg.patch_data.ga2 > 0) patch["ga2"] = msg.patch_data.ga2;
    if (msg.patch_data.ga3 > 0) patch["ga3"] = msg.patch_data.ga3;
    j["patch"] = patch;
  }

  // Metadata string from parser (contains decoded detail for some opcodes)
  if (!msg.meta.empty()) {
    j["meta"] = msg.meta;
  }

  // Write NDJSON line to stdout
  std::cout << j.dump(-1, ' ', false, json::error_handler_t::replace) << "\n";
  std::cout.flush();
}

void CCLogger::log_decode_rate(int msgs_per_sec, double control_freq) {
  json j;
  j["ts"] = now_iso8601();
  j["sys"] = short_name_;
  j["type"] = "_DECODE_RATE";
  j["msgs_per_sec"] = msgs_per_sec;
  if (control_freq > 0) {
    j["control_freq"] = control_freq / 1e6;
  }

  std::cout << j.dump() << "\n";
  std::cout.flush();
}

void CCLogger::log_event(const std::string &event_type, const std::string &detail) {
  json j;
  j["ts"] = now_iso8601();
  j["sys"] = short_name_;
  j["type"] = "_EVENT";
  j["event"] = event_type;
  if (!detail.empty()) {
    j["detail"] = detail;
  }

  std::cout << j.dump() << "\n";
  std::cout.flush();
}
