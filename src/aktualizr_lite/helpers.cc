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

static Uptane::Target finalizeIfNeeded(INvStorage &storage, PackageConfig &config) {
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
      return target;
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
      return *it;
    }
  }
  return Uptane::Target::Unknown();
}

LiteClient::LiteClient(Config &config_in) : config(std::move(config_in)) {
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
    ecu_serials.emplace_back(Uptane::EcuSerial(serial), Uptane::HardwareIdentifier(hwid));
    storage->storeEcuSerials(ecu_serials);
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

  Uptane::Target tgt = finalizeIfNeeded(*storage, config.pacman);
  headers.emplace_back("x-ats-target: " + tgt.filename());
  headers.emplace_back("x-ats-tags: " + boost::algorithm::join(config.pacman.tags, ","));

  auto http_client = std::make_shared<HttpClient>(&headers);

  KeyManager keys(storage, config.keymanagerConfig());
  keys.copyCertsToCurl(*http_client);

  primary = std::make_shared<SotaUptaneClient>(config, storage, http_client);
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
