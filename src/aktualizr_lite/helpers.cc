#include "helpers.h"

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "package_manager/ostreemanager.h"

#ifdef BUILD_DOCKERAPP
static void add_apps_header(std::vector<std::string> &headers, PackageConfig &config) {
  if (config.type == PackageManager::kOstreeDockerApp) {
    headers.emplace_back("x-ats-dockerapps: " + boost::algorithm::join(config.docker_apps, ","));
  }
}
#else
#define add_apps_header(headers, config) \
  {}
#endif

static std::pair<bool, Uptane::Target> finalizeIfNeeded(INvStorage &storage, PackageConfig &config) {
  boost::optional<Uptane::Target> pending_version;
  storage.loadInstalledVersions("", nullptr, &pending_version);

  GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.sysroot);
  OstreeDeployment *booted_deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
  std::string current_hash = ostree_deployment_get_csum(booted_deployment);
  if (booted_deployment == nullptr) {
    throw std::runtime_error("Could not get booted deployment in " + config.sysroot.string());
  }

  if (!!pending_version) {
    const Uptane::Target &target = *pending_version;
    if (current_hash == target.sha256Hash()) {
      LOG_INFO << "Marking target install complete for: " << target;
      storage.saveInstalledVersion("", target, InstalledVersionUpdateMode::kCurrent);
      return std::make_pair(true, target);
    }
  }

  std::vector<Uptane::Target> installed_versions;
  storage.loadPrimaryInstallationLog(&installed_versions, false);

  // Version should be in installed versions. Its possible that multiple
  // targets could have the same sha256Hash. In this case the safest assumption
  // is that the most recent (the reverse of the vector) target is what we
  // should return.
  std::vector<Uptane::Target>::reverse_iterator it;
  for (it = installed_versions.rbegin(); it != installed_versions.rend(); it++) {
    if (it->sha256Hash() == current_hash) {
      return std::make_pair(false, *it);
    }
  }
  return std::make_pair(false, Uptane::Target::Unknown());
}

LiteClient::LiteClient(Config &config_in) : config(std::move(config_in)), primary_serial(Uptane::EcuSerial::Unknown()) {
  std::string pkey;
  storage = INvStorage::newStorage(config.storage);
  storage->importData(config.import);

  EcuSerials ecu_serials;
  if (!storage->loadEcuSerials(&ecu_serials)) {
    // Set a "random" serial so we don't get warning messages.
    std::string serial = config.provision.primary_ecu_serial;
    std::string hwid = config.provision.primary_ecu_hardware_id;
    if (hwid.empty()) {
      hwid = Utils::getHostname();
    }
    if (serial.empty()) {
      boost::uuids::uuid tmp = boost::uuids::random_generator()();
      serial = boost::uuids::to_string(tmp);
    }
    primary_serial = Uptane::EcuSerial(serial);
    ecu_serials.emplace_back(primary_serial, Uptane::HardwareIdentifier(hwid));
    storage->storeEcuSerials(ecu_serials);
  } else {
    primary_serial = ecu_serials[0].first;
  }

  std::vector<std::string> headers;
  GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.pacman.sysroot);
  OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
  std::string header("x-ats-ostreehash: ");
  if (deployment != nullptr) {
    header += ostree_deployment_get_csum(deployment);
  } else {
    header += "?";
  }
  headers.push_back(header);
  add_apps_header(headers, config.pacman);

  std::pair<bool, Uptane::Target> pair = finalizeIfNeeded(*storage, config.pacman);
  headers.emplace_back("x-ats-target: " + pair.second.filename());
  headers.emplace_back("x-ats-tags: " + boost::algorithm::join(config.pacman.tags, ","));

  auto http_client = std::make_shared<HttpClient>(&headers);
  report_queue = std_::make_unique<ReportQueue>(config, http_client);

  if (pair.first) {
    notifyInstallFinished(pair.second, data::ResultCode::Numeric::kOk);
  }

  KeyManager keys(storage, config.keymanagerConfig());
  keys.copyCertsToCurl(*http_client);

  primary = std::make_shared<SotaUptaneClient>(config, storage, http_client);
}

void LiteClient::notify(const Uptane::Target &t, std::unique_ptr<ReportEvent> event) {
  if (!config.tls.server.empty()) {
    event->custom["targetName"] = t.filename();
    event->custom["version"] = t.custom_version();
    report_queue->enqueue(std::move(event));
  }
}

void LiteClient::notifyDownloadStarted(const Uptane::Target &t) {
  notify(t, std_::make_unique<EcuDownloadStartedReport>(primary_serial, t.correlation_id()));
}

void LiteClient::notifyDownloadFinished(const Uptane::Target &t, bool success) {
  notify(t, std_::make_unique<EcuDownloadCompletedReport>(primary_serial, t.correlation_id(), success));
}

void LiteClient::notifyInstallStarted(const Uptane::Target &t) {
  notify(t, std_::make_unique<EcuInstallationStartedReport>(primary_serial, t.correlation_id()));
}

void LiteClient::notifyInstallFinished(const Uptane::Target &t, data::ResultCode::Numeric rc) {
  if (rc == data::ResultCode::Numeric::kNeedCompletion) {
    notify(t, std_::make_unique<EcuInstallationAppliedReport>(primary_serial, t.correlation_id()));
  } else if (rc == data::ResultCode::Numeric::kOk) {
    notify(t, std_::make_unique<EcuInstallationCompletedReport>(primary_serial, t.correlation_id(), true));
  } else {
    notify(t, std_::make_unique<EcuInstallationCompletedReport>(primary_serial, t.correlation_id(), false));
  }
}

void generate_correlation_id(Uptane::Target &t) {
  std::string id = t.custom_version();
  if (id.empty()) {
    id = t.filename();
  }
  boost::uuids::uuid tmp = boost::uuids::random_generator()();
  t.setCorrelationId(id + "-" + boost::uuids::to_string(tmp));
}

bool target_has_tags(const Uptane::Target &t, const std::vector<std::string> &config_tags) {
  if (!config_tags.empty()) {
    auto tags = t.custom_data()["tags"];
    for (Json::ValueIterator i = tags.begin(); i != tags.end(); ++i) {
      auto tag = (*i).asString();
      if (std::find(config_tags.begin(), config_tags.end(), tag) != config_tags.end()) {
        return true;
      }
    }
    return false;
  }
  return true;
}

bool targets_eq(const Uptane::Target &t1, const Uptane::Target &t2, bool compareDockerApps) {
  // target equality check looks at hashes
  if (t1.MatchTarget(t2)) {
    if (compareDockerApps) {
      auto t1_apps = t1.custom_data()["docker_apps"];
      auto t2_apps = t2.custom_data()["docker_apps"];
      for (Json::ValueIterator i = t1_apps.begin(); i != t1_apps.end(); ++i) {
        auto app = i.key().asString();
        if (!t2_apps.isMember(app)) {
          return false;  // an app has been removed
        }
        if ((*i)["filename"].asString() != t2_apps[app]["filename"].asString()) {
          return false;  // tuf target filename changed
        }
        t2_apps.removeMember(app);
      }
      if (t2_apps.size() > 0) {
        return false;  // an app has been added
      }
    }
    return true;  // docker apps are the same, or there are none
  }
  return false;
}
