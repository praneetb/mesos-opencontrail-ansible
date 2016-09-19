#include <mesos/hook.hpp>
#include <mesos/mesos.hpp>
#include <mesos/module.hpp>

#include <mesos/module/hook.hpp>

#include <process/future.hpp>
#include <process/id.hpp>
#include <process/process.hpp>
#include <process/protobuf.hpp>
#include <process/subprocess.hpp>

#include <stout/foreach.hpp>
#include <stout/os.hpp>
#include <stout/try.hpp>

#include <mesos/mesos.pb.h>

#include <fstream>

template <typename T>
static std::string getValue(const T &obj)
{
  if (obj.has_value()) {
    return obj.value();
  }
  return "?";
}

std::string taskStateToString(mesos::TaskState state) {
  switch (state) {
  case mesos::TASK_STAGING:
    return "STAGING";
    break;
  case mesos::TASK_STARTING:
    return "STARTING";
    break;
  case mesos::TASK_RUNNING:
    return "RUNNING";
    break;
  case mesos::TASK_KILLING:
    return "KILLING";
    break;
  case mesos::TASK_FINISHED:
    return "FINISHED";
    break;
  case mesos::TASK_FAILED:
    return "FAILED";
    break;
  case mesos::TASK_KILLED:
    return "KILLED";
    break;
  case mesos::TASK_LOST:
    return "LOST";
    break;
  case mesos::TASK_ERROR:
    return "ERROR";
    break;
  default:
    return "UNKNOWN";
  }
}

static JSON::Value parseJsonArray(const std::string &str, int index)
{
  Try<JSON::Value> maybe_json = JSON::parse(str);
  if (maybe_json.isError()) {
    LOG(WARNING) << maybe_json.error();
    return "?";
  }
  if (!maybe_json.get().is<JSON::Array>()) {
    LOG(WARNING) << "Json is not an array.";
    return "?";
  }
  const JSON::Array &arr = maybe_json.get().as<JSON::Array>();
  return arr.values[index];
}

static std::string getJsonObjectValue(const JSON::Value &value, const std::string &key)
{
  if (!value.is<JSON::Object>()) {
    LOG(WARNING) << "Value is not an object.";
    return "?";
  }
  const JSON::Object &obj = value.as<JSON::Object>();
  auto result = obj.find<JSON::Value>(key);
  if (result.isError()) {
    LOG(WARNING) << result.error();
    return "?";
  }
  if (result.isNone()) {
    LOG(WARNING) << "No such nested label (" << key << ")";
    return "?";
  }
  std::stringstream ss;
  ss << result.get();
  return ss.str();
}

static std::string getLabelValue(
    const mesos::Labels &labels, const std::string &key, const std::string &nested)
{
  assert(!key.empty());

  for (int i = 0; i < labels.labels_size(); ++i) {
    if (labels.labels(i).key() == key) {
      auto &value = labels.labels(i).value();
      if (nested.empty()) {
        return value;
      }
      Try<JSON::Value> maybe_json = JSON::parse(value);
      if (maybe_json.isError()) {
        LOG(WARNING) << "Parsing json: " << maybe_json.error();
        return "?";
      }
      return getJsonObjectValue(maybe_json.get(), nested);
    }
  }
  return "?";
}

class TestHook : public mesos::Hook
{
public:
	Result<mesos::TaskStatus> slaveTaskStatusDecorator(
		const mesos::FrameworkID& frameworkId,
		const mesos::TaskStatus& status) {
      if (!status.has_state()) {
        return None();
      }
      std::string state_str = taskStateToString(status.state());
      std::string container_id = getJsonObjectValue(parseJsonArray(status.data(), 0), "Id");

      std::string cmd = std::string("/usr/local/sbin/pack.sh")
          + " " + state_str
          + " " + (status.has_task_id() ? getValue(status.task_id()) : "?")
          + " " + getValue(frameworkId)
          + " " + container_id
          + " " + getLabelValue(status.labels(), "service", "")
          + " " + getLabelValue(status.labels(), "host_to_add", "")
          + " " + getLabelValue(status.labels(), "network", "")
          + " " + getLabelValue(status.labels(), "policy", "");
      LOG(INFO) << "COMMAND: " << cmd;
      Try<process::Subprocess> hookCmd = process::subprocess(cmd);
      if (hookCmd.isError()) {
        LOG(ERROR) << "Forking hook process failed: " << hookCmd.error();
      } else {
        hookCmd.get().status().discard();
      }
			return None();
	}
};

static mesos::Hook* createHook(const mesos::Parameters& /*parameters*/)
{
  return new TestHook();
}

mesos::modules::Module<mesos::Hook> HookModule(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Adam Niezurawski",
    "adam.niezurawski@codilime.com",
    "Test Hook module.",
    nullptr,
    createHook);
