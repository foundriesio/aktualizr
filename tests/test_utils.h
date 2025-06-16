#ifndef TEST_UTILS_H_
#define TEST_UTILS_H_

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "utilities/utils.h"

#include <boost/version.hpp>
#if BOOST_VERSION >= 108800
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/async.hpp>
#include <boost/process/v1/async_system.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/cmd.hpp>
#include <boost/process/v1/env.hpp>
#include <boost/process/v1/environment.hpp>
#include <boost/process/v1/error.hpp>
#include <boost/process/v1/exe.hpp>
#include <boost/process/v1/group.hpp>
#include <boost/process/v1/handles.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/pipe.hpp>
#include <boost/process/v1/search_path.hpp>
#include <boost/process/v1/shell.hpp>
#include <boost/process/v1/spawn.hpp>
#include <boost/process/v1/start_dir.hpp>
#include <boost/process/v1/system.hpp>
namespace bp = boost::process::v1;
#else
#include <boost/process.hpp>
namespace bp = boost::process;
#endif

struct TestUtils {
  static std::string getFreePort();
  static in_port_t getFreePortAsInt();
  static void writePathToConfig(const boost::filesystem::path &toml_in, const boost::filesystem::path &toml_out,
                                const boost::filesystem::path &storage_path);
  static void waitForServer(const std::string &address);
};

class Process {
 public:
  using Result = std::tuple<int, std::string, std::string>;

  static Result spawn(const std::string &executable_to_run, const std::vector<std::string> &executable_args);

  Process(const std::string &exe_path) : exe_path_(exe_path) {}

  Process::Result run(const std::vector<std::string> &args);

  int lastExitCode() const { return last_exit_code_; }

  const std::string &lastStdOut() const { return last_stdout_; }

  const std::string &lastStdErr() const { return last_stderr_; }

 private:
  const std::string exe_path_;

  int last_exit_code_;
  std::string last_stdout_;
  std::string last_stderr_;
};

#endif  // TEST_UTILS_H_
