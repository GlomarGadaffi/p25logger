/**
 * p25-cc-logger: A lean P25 control channel data logger.
 *
 * Decodes the P25 control channel and outputs every TSBK/MBT message
 * as structured NDJSON to stdout. No audio recording, no plugins,
 * no call management.
 *
 * Usage:
 *   p25-cc-logger --config /path/to/config.json
 *   p25-cc-logger --config config.json 2>/dev/null | jq .
 *   p25-cc-logger --config config.json >> cc_log.ndjson
 */

#include <boost/algorithm/string.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/program_options.hpp>

#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gnuradio/top_block.h>
#include <osmosdr/source.h>

#include "config.h"
#include "formatter.h"
#include "source.h"
#include "systems/system.h"
#include "systems/system_impl.h"
#include "systems/p25_trunking.h"
#include "systems/p25_parser.h"
#include "cc_logger.h"

using namespace std;

volatile sig_atomic_t exit_flag = 0;

void signal_handler(int sig) {
  exit_flag = 1;
}

int main(int argc, char **argv) {
  // Redirect Boost.Log to stderr so stdout is clean NDJSON
  namespace logging = boost::log;
  logging::core::get()->set_filter(logging::trivial::severity >= logging::trivial::info);
  boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");
  boost::log::add_common_attributes();

  // Log to stderr
  auto console_sink = logging::add_console_log(std::cerr);
  console_sink->set_formatter(logging::expressions::format("[%1%] (%2%)   %3%") %
    logging::expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S") %
    logging::expressions::attr<logging::trivial::severity_level>("Severity") %
    logging::expressions::smessage);

  // Parse command line
  boost::program_options::options_description desc("P25 Control Channel Logger");
  desc.add_options()
    ("help,h", "Show help")
    ("config,c", boost::program_options::value<string>()->default_value("./config.json"), "Config file path");

  boost::program_options::variables_map vm;
  boost::program_options::store(parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  if (vm.count("help")) {
    std::cerr << "Usage: p25-cc-logger [options]\n" << desc << std::endl;
    return 0;
  }

  string config_file = vm["config"].as<string>();

  // Load config using trunk-recorder's config loader
  Config config;
  std::vector<Source *> sources;
  std::vector<System *> systems;
  gr::top_block_sptr tb = gr::make_top_block("P25-CC-Logger");

  BOOST_LOG_TRIVIAL(info) << "P25 Control Channel Logger starting...";
  BOOST_LOG_TRIVIAL(info) << "Config: " << config_file;

  if (!load_config(config_file, config, tb, sources, systems)) {
    BOOST_LOG_TRIVIAL(error) << "Failed to load config file: " << config_file;
    return 1;
  }

  // Validate we have exactly what we need
  if (sources.empty()) {
    BOOST_LOG_TRIVIAL(error) << "No sources configured!";
    return 1;
  }

  if (systems.empty()) {
    BOOST_LOG_TRIVIAL(error) << "No systems configured!";
    return 1;
  }

  // We only support P25 systems
  System_impl *system = nullptr;
  for (auto *sys : systems) {
    if (sys->get_system_type() == "p25") {
      system = (System_impl *)sys;
      break;
    }
  }

  if (!system) {
    BOOST_LOG_TRIVIAL(error) << "No P25 system found in config!";
    return 1;
  }

  BOOST_LOG_TRIVIAL(info) << "System: " << system->get_short_name();

  // Find a source covering the control channel
  double control_channel_freq = system->get_current_control_channel();
  Source *source = nullptr;

  for (auto *src : sources) {
    if (src->get_min_hz() <= control_channel_freq &&
        src->get_max_hz() >= control_channel_freq) {
      source = src;
      break;
    }
  }

  if (!source) {
    BOOST_LOG_TRIVIAL(error) << "No source covers control channel freq "
                             << format_freq(control_channel_freq);
    return 1;
  }

  BOOST_LOG_TRIVIAL(info) << "Source: " << source->get_driver() << " " << source->get_device();
  BOOST_LOG_TRIVIAL(info) << "Control channel: " << format_freq(control_channel_freq);

  // Wire up the P25 trunking block
  system->set_source(source);
  system->p25_trunking = make_p25_trunking(
    control_channel_freq,
    source->get_center(),
    source->get_rate(),
    system->get_msg_queue(),
    system->get_qpsk_mod(),
    system->get_sys_num()
  );

  tb->connect(source->get_src_block(), 0, system->p25_trunking, 0);

  // Create the logger
  CCLogger logger;
  logger.set_short_name(system->get_short_name());

  // Set up talkgroup lookup (system loads talkgroups in config)
  // The talkgroups are accessible via system->find_talkgroup()
  // We pass the system's talkgroup data to the logger indirectly

  // Create the P25 parser
  P25Parser p25_parser;

  // Load custom frequency table if specified
  if (system->has_custom_freq_table_file()) {
    p25_parser.load_freq_table(system->get_custom_freq_table_file(), system->get_sys_num());
  }

  // Set up signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Start the flow graph
  BOOST_LOG_TRIVIAL(info) << "Starting GNU Radio flow graph...";
  tb->start();

  logger.log_event("START", "P25 CC Logger started on " + format_freq(control_channel_freq));

  // Main loop — poll the message queue, parse, and log
  time_t last_decode_check = time(NULL);
  int message_count = 0;

  BOOST_LOG_TRIVIAL(info) << "Listening... (NDJSON output on stdout)";

  while (!exit_flag) {
    // Poll for messages from the P25 trunking block
    gr::message::sptr msg = system->get_msg_queue()->delete_head_nowait();

    while (msg != nullptr) {
      message_count++;

      // Parse the raw message into TrunkMessages
      std::vector<TrunkMessage> trunk_messages = p25_parser.parse_message(msg, system);

      // Log every parsed message
      for (const auto &tm : trunk_messages) {
        logger.log_message(tm);
      }

      msg.reset();
      msg = system->get_msg_queue()->delete_head_nowait();
    }

    // Periodic decode rate check (every 3 seconds)
    time_t now = time(NULL);
    float time_diff = difftime(now, last_decode_check);

    if (time_diff >= 3.0) {
      int msgs_per_sec = (int)(message_count / time_diff);
      system->set_decode_rate(msgs_per_sec);

      logger.log_decode_rate(msgs_per_sec, system->get_current_control_channel());

      // Check for loss of control channel
      if (msgs_per_sec < 2) {
        BOOST_LOG_TRIVIAL(warning) << "Low decode rate: " << msgs_per_sec << " msg/sec";
        logger.log_event("LOW_DECODE_RATE", std::to_string(msgs_per_sec) + " msg/sec");

        // Try retuning to next control channel
        if (system->control_channel_count() > 1) {
          double next_cc = system->get_next_control_channel();
          BOOST_LOG_TRIVIAL(info) << "Retuning to control channel: " << format_freq(next_cc);
          logger.log_event("RETUNE", format_freq(next_cc));

          // Check if current source covers the new control channel
          if (source->get_min_hz() <= next_cc && source->get_max_hz() >= next_cc) {
            system->p25_trunking->tune_freq(next_cc);
          } else {
            BOOST_LOG_TRIVIAL(error) << "Next control channel not covered by source!";
            logger.log_event("ERROR", "Control channel " + format_freq(next_cc) + " not covered by source");
          }
        }
      }

      message_count = 0;
      last_decode_check = now;
    }

    // Sleep briefly to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Shutdown
  logger.log_event("STOP", "P25 CC Logger shutting down");
  BOOST_LOG_TRIVIAL(info) << "Shutting down...";

  tb->stop();
  tb->wait();

  BOOST_LOG_TRIVIAL(info) << "Done.";
  return 0;
}
