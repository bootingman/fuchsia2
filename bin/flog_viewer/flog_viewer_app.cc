// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <limits>
#include <vector>

#include "apps/media/tools/flog_viewer/channel_handler.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

namespace flog {

class FlogViewerApp {
 public:
  FlogViewerApp(int argc, const char** argv) {
    ProcessArgs(argc, argv);

    std::unique_ptr<modular::ApplicationContext> application_context =
        modular::ApplicationContext::CreateFromStartupInfo();

    viewer_.Initialize(application_context.get(), []() {
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    });

    if (delete_all_logs_) {
      viewer_.DeleteAllLogs();
    } else {
      for (uint32_t log_to_delete : logs_to_delete_) {
        viewer_.DeleteLog(log_to_delete);
      }
    }

    if (process_last_ || !process_last_label_.empty()) {
      viewer_.ProcessLastLog(process_last_label_);
    } else if (log_id_ == 0) {
      viewer_.ProcessLogs();
    } else {
      viewer_.ProcessLog(log_id_);
    }
  }

 private:
  // Processes arguments.
  void ProcessArgs(int argc, const char** argv) {
    for (int i = 0; i < argc; ++i) {
      const std::string& arg = argv[i];

      std::string string_value;
      uint32_t numeric_value;
      std::vector<uint32_t> numeric_values;

      if (MatchOption(arg, "format", &string_value)) {
        viewer_.set_format(string_value);
      } else if (MatchOption(arg, "channel", &numeric_values) ||
                 MatchOption(arg, "channels", &numeric_values)) {
        for (uint32_t value : numeric_values) {
          viewer_.AddChannel(value);
        }
      } else if (MatchOption(arg, "last", &string_value)) {
        process_last_label_ = string_value;
      } else if (MatchOption(arg, "last")) {
        process_last_ = true;
      } else if (MatchOption(arg, "delete-log", &numeric_values) ||
                 MatchOption(arg, "delete-logs", &numeric_values)) {
        for (uint32_t value : numeric_values) {
          logs_to_delete_.push_back(value);
        }
      } else if (MatchOption(arg, "delete-all-logs")) {
        delete_all_logs_ = true;
      } else if (MatchBareArg(arg, &numeric_value)) {
        log_id_ = numeric_value;
      } else {
        Usage();
      }
    }
  }

  void Usage() {
    std::cout << std::endl;
    std::cout << "fidl:flog_viewer <args>\"" << std::endl;
    std::cout << "    <log id>                    process specified log"
              << std::endl;
    std::cout << "    --last=<label>              process last log with "
                 "specified label"
              << std::endl;
    std::cout
        << "    --last                      process last log with any label"
        << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "    --format=<format>           terse (default), full, digest"
              << std::endl;
    std::cout
        << "    --channel(s)=<channel ids>  process only the indicated channels"
        << std::endl;
    std::cout << "    --delete-log(s)=<log ids>   delete the indicated logs"
              << std::endl;
    std::cout << "    --delete-all-logs           delete all logs" << std::endl;
    std::cout << "If no log is specified, a list of logs is printed."
              << std::endl;
    std::cout << "Value lists are comma-separated (channel ids, log ids)."
              << std::endl;
    std::cout << std::endl;

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  bool MatchOption(const std::string& arg, const std::string& option) {
    return arg.compare(0, 2, "--") == 0 &&
           arg.compare(2, std::string::npos, option) == 0;
  }

  bool MatchOption(const std::string& arg,
                   const std::string& option,
                   std::string* string_out) {
    FTL_DCHECK(string_out);

    if (arg.compare(0, 2, "--") != 0 ||
        arg.compare(2, option.size(), option) != 0 ||
        arg.compare(2 + option.size(), 1, "=") != 0) {
      return false;
    }

    *string_out = arg.substr(2 + option.size() + 1);

    return true;
  }

  bool MatchOption(const std::string& arg,
                   const std::string& option,
                   std::vector<uint32_t>* vector_of_uint32_out) {
    FTL_DCHECK(vector_of_uint32_out);

    if (arg.compare(0, 2, "--") != 0 ||
        arg.compare(2, option.size(), option) != 0 ||
        arg.compare(2 + option.size(), 1, "=") != 0) {
      return false;
    }

    std::istringstream istream(arg.substr(2 + option.size() + 1));
    uint32_t value;
    while (istream >> value) {
      vector_of_uint32_out->push_back(value);
      if (istream.peek() == ',') {
        istream.ignore();
      }
    }

    return vector_of_uint32_out->size() != 0 && istream.eof();
  }

  bool MatchBareArg(const std::string& arg, uint32_t* uint32_out) {
    FTL_DCHECK(uint32_out);

    std::istringstream istream(arg);
    return static_cast<bool>(istream >> *uint32_out);
  }

  FlogViewer viewer_;
  uint32_t log_id_ = 0;
  bool process_last_ = false;
  std::string process_last_label_;
  bool delete_all_logs_ = false;
  std::vector<uint32_t> logs_to_delete_;
};

}  // namespace flog

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  flog::FlogViewerApp app(argc, argv);

  loop.Run();
  return 0;
}
