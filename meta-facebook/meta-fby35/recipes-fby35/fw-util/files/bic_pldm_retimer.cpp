#include "bic_pldm_retimer.hpp"

#include <openbmc/kv.hpp>
#include <openbmc/pal.h>

#include <iostream>
#include <regex>

using namespace std;

int PldmRetimerComponent::update_version_cache() {
  string activeVersion, pendingVersion;

  if (PldmComponent::update_version_cache()) {
    return FW_STATUS_FAILURE;
  }
  activeVersion = kv::get(activeVersionKey, kv::region::temp);
  pendingVersion = kv::get(pendingVersionKey, kv::region::temp);

  // When the BIC fails to access this component, it returns "ERROR:%d."
  if (activeVersion.find("ERROR:") != string::npos || 
      pendingVersion.find("ERROR:") != string::npos) {   
    // Set as INVALID_VERSION so that the update version cache is triggered next time
    kv::set(activeVersionKey, INVALID_VERSION, kv::region::temp);
    kv::set(pendingVersionKey, INVALID_VERSION, kv::region::temp);
  }
  return FW_STATUS_SUCCESS;
}

int PldmRetimerComponent::update_internal(string image, bool force) {
  try {
    server.ready();
    uint8_t status;
    if (pal_get_server_power(slot_id, &status)) {
      throw string("Failed to get server power");
    }
    if (status != SERVER_POWER_ON) {
      throw string("The server power is not turned on");
    }
  } catch (const string& e) {
    cerr << e << endl;
    return FW_STATUS_FAILURE;
  }

  return PldmComponent::update_internal(image, force);
}

int PldmRetimerComponent::get_version(json& j) {
  try {
    server.ready();
  } catch(const string& e) {
    throw runtime_error(e);
  }
  
  if (PldmComponent::get_version(j)) {
    return FW_STATUS_FAILURE;
  }
  auto activeVersion = kv::get(activeVersionKey, kv::region::temp);
  auto pendingVersion = kv::get(pendingVersionKey, kv::region::temp);
  
  // If the active version is valid but the pending version is invalid, let 
  // the pending version equal the active version
  if (activeVersion.find(INVALID_VERSION) == string::npos && 
      pendingVersion.find(INVALID_VERSION) != string::npos) {
    pendingVersion = activeVersion;
    kv::set(pendingVersionKey, pendingVersion, kv::region::temp);
  }

  // Retimer active version format is "<VENDOR NAME> <VERSION>"
  // Retimer pending version format is "<DEVICE NAME> <VERSION>_<COMPONENT>"
  // Replace pending version <DEVICE NAME> with active version <VENDOR NAME>
  regex pattern(R"((\S+)\s([^_]+)(?:_.*)?)");
  smatch matches;
  string vendor = INVALID_VERSION;
  if (regex_search(activeVersion, matches, pattern)) {
    vendor = matches[1].str();
    activeVersion = matches[2].str();
  }
  matches = smatch();
  if (regex_search(pendingVersion, matches, pattern) && !vendor.empty()) {
    pendingVersion = vendor + " " + matches[2].str();
    kv::set(pendingVersionKey, pendingVersion, kv::region::temp);
  }
  
  j[VERSION] = activeVersion;
  j["VENDOR"] = vendor;

  return FW_STATUS_SUCCESS;
}