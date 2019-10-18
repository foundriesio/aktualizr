#include "dockerappmanager.h"

#include <sstream>

struct DockerApp {
  DockerApp(std::string app_name, const PackageConfig &config)
      : name(std::move(app_name)),
        app_root(config.docker_apps_root / name),
        app_params(config.docker_app_params),
        app_bin(config.docker_app_bin),
        compose_bin(config.docker_compose_bin) {}

  bool render(const std::string &app_content, bool persist) {
    auto bin = boost::filesystem::canonical(app_bin).string();
    Utils::writeFile(app_root / (name + ".dockerapp"), app_content);
    std::string cmd("cd " + app_root.string() + " && " + bin + " render " + name);
    if (!app_params.empty()) {
      cmd += " -f " + app_params.string();
    }
    std::string yaml;
    if (Utils::shell(cmd, &yaml, true) != 0) {
      LOG_ERROR << "Unable to run " << cmd << " output:\n" << yaml;
      return false;
    }
    if (persist) {
      Utils::writeFile(app_root / "docker-compose.yml", yaml);
    }
    return true;
  }

  bool start() {
    // Depending on the number and size of the containers in the docker-app,
    // this command can take a bit of time to complete. Rather than using,
    // Utils::shell which isn't interactive, we'll use std::system so that
    // stdout/stderr is streamed while docker sets things up.
    auto bin = boost::filesystem::canonical(compose_bin).string();
    std::string cmd("cd " + app_root.string() + " && " + bin + " up --remove-orphans -d");
    return std::system(cmd.c_str()) == 0;
  }

  void remove() {
    auto bin = boost::filesystem::canonical(compose_bin).string();
    std::string cmd("cd " + app_root.string() + " && " + bin + " down");
    if (std::system(cmd.c_str()) == 0) {
      boost::filesystem::remove_all(app_root);
    } else {
      LOG_ERROR << "docker-compose was unable to bring down: " << app_root;
    }
  }

  std::string name;
  boost::filesystem::path app_root;
  boost::filesystem::path app_params;
  boost::filesystem::path app_bin;
  boost::filesystem::path compose_bin;
};

bool DockerAppManager::iterate_apps(const Uptane::Target &target, const DockerAppCb &cb) const {
  auto apps = target.custom_data()["docker_apps"];
  bool res = true;
  Uptane::ImagesRepository repo;
  // checkMetaOffline pulls in data from INvStorage to properly initialize
  // the targets member of the instance so that we can use the LazyTargetList
  repo.checkMetaOffline(*storage_);

  if (!apps) {
    LOG_DEBUG << "Detected an update target from Director with no docker-apps data";
    for (const auto &t : Uptane::LazyTargetsList(repo, storage_, fake_fetcher_)) {
      if (t.MatchTarget(target)) {
        LOG_DEBUG << "Found the match " << t;
        apps = t.custom_data()["docker_apps"];
        break;
      }
    }
  }

  for (const auto &t : Uptane::LazyTargetsList(repo, storage_, fake_fetcher_)) {
    for (Json::ValueIterator i = apps.begin(); i != apps.end(); ++i) {
      if ((*i).isObject() && (*i).isMember("filename")) {
        for (const auto &app : config.docker_apps) {
          if (i.key().asString() == app && (*i)["filename"].asString() == t.filename()) {
            if (!cb(app, t)) {
              res = false;
            }
          }
        }
      } else {
        LOG_ERROR << "Invalid custom data for docker-app: " << i.key().asString() << " -> " << *i;
      }
    }
  }
  return res;
}

bool DockerAppManager::fetchTarget(const Uptane::Target &target, Uptane::Fetcher &fetcher, const KeyManager &keys,
                                   FetcherProgressCb progress_cb, const api::FlowControlToken *token) {
  if (!OstreeManager::fetchTarget(target, fetcher, keys, progress_cb, token)) {
    return false;
  }

  LOG_INFO << "Looking for DockerApps to fetch";
  auto cb = [this, &fetcher, &keys, progress_cb, token](const std::string &app, const Uptane::Target &app_target) {
    LOG_INFO << "Fetching " << app << " -> " << app_target;
    return PackageManagerInterface::fetchTarget(app_target, fetcher, keys, progress_cb, token);
  };
  return iterate_apps(target, cb);
}

data::InstallationResult DockerAppManager::install(const Uptane::Target &target) const {
  auto res = OstreeManager::install(target);
  handleRemovedApps();
  auto cb = [this](const std::string &app, const Uptane::Target &app_target) {
    LOG_INFO << "Installing " << app << " -> " << app_target;
    std::stringstream ss;
    ss << *storage_->openTargetFile(app_target);
    DockerApp dapp(app, config);
    return dapp.render(ss.str(), true) && dapp.start();
  };
  if (!iterate_apps(target, cb)) {
    return data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "Could not render docker app");
  }
  return res;
}

// Handle the case like:
//  1) sota.toml is configured with 2 docker apps: "app1, app2"
//  2) update is applied, so we are now running both app1 and app2
//  3) sota.toml is updated with 1 docker app: "app1"
// At this point we should stop app2 and remove it.
void DockerAppManager::handleRemovedApps() const {
  for (auto &entry : boost::make_iterator_range(boost::filesystem::directory_iterator(config.docker_apps_root), {})) {
    if (boost::filesystem::is_directory(entry)) {
      std::string name = entry.path().filename().native();
      if (std::find(config.docker_apps.begin(), config.docker_apps.end(), name) == config.docker_apps.end()) {
        LOG_WARNING << "Docker App(" << name
                    << ") installed, but is now removed from configuration. Removing from system";
        DockerApp(name, config).remove();
      }
    }
  }
}

TargetStatus DockerAppManager::verifyTarget(const Uptane::Target &target) const {
  TargetStatus status;
  if (target.IsOstree()) {
    status = OstreeManager::verifyTarget(target);
    if (status != TargetStatus::kGood) {
      return status;
    }
  }
  auto cb = [this](const std::string &app, const Uptane::Target &app_target) {
    LOG_INFO << "Verifying " << app << " -> " << app_target;
    std::stringstream ss;
    ss << *storage_->openTargetFile(app_target);
    DockerApp dapp(app, config);
    return dapp.render(ss.str(), false);
  };
  if (!iterate_apps(target, cb)) {
    return TargetStatus::kInvalid;
  }
  return TargetStatus::kGood;
}
