#ifndef P11ENGINE_H_
#define P11ENGINE_H_

#include <memory>

#include "libaktualizr/config.h"

#include <openssl/err.h>
#include "gtest/gtest_prod.h"

#include "crypto/openssl_compat.h"
#include "logging/logging.h"

#if AKTUALIZR_OPENSSL_NO_ENGINE
#include <openssl/evp.h>
#else
#include <openssl/engine.h>
#endif

class P11ContextWrapper {
 public:
  explicit P11ContextWrapper(const boost::filesystem::path &module);
  ~P11ContextWrapper();  // NOLINT(performance-trivially-destructible)
  P11ContextWrapper(const P11ContextWrapper &) = delete;
  P11ContextWrapper(P11ContextWrapper &&) = delete;
  P11ContextWrapper &operator=(const P11ContextWrapper &) = delete;
  P11ContextWrapper &operator=(P11ContextWrapper &&) = delete;
  PKCS11_ctx_st *get() const { return ctx; }

 private:
  PKCS11_ctx_st *ctx;
};

class P11SlotsWrapper {
 public:
  explicit P11SlotsWrapper(PKCS11_ctx_st *ctx_in);
  ~P11SlotsWrapper();  // NOLINT(performance-trivially-destructible)
  P11SlotsWrapper(const P11SlotsWrapper &) = delete;
  P11SlotsWrapper(P11SlotsWrapper &&) = delete;
  P11SlotsWrapper &operator=(const P11SlotsWrapper &) = delete;
  P11SlotsWrapper &operator=(P11SlotsWrapper &&) = delete;
  PKCS11_slot_st *get_slots() const { return slots_; }
  unsigned int get_nslots() const { return nslots; }

 private:
  PKCS11_ctx_st *ctx;
  PKCS11_slot_st *slots_;
  unsigned int nslots;
};

class P11EngineGuard;

class P11Engine {
 public:
  P11Engine(const P11Engine &) = delete;
  P11Engine(P11Engine &&) = delete;
  P11Engine &operator=(const P11Engine &) = delete;
  P11Engine &operator=(P11Engine &&) = delete;

#if AKTUALIZR_OPENSSL_NO_ENGINE
  virtual ~P11Engine() = default;
#else
  virtual ~P11Engine() {
    if (ssl_engine_ != nullptr) {
      ENGINE_finish(ssl_engine_);
      ENGINE_free(ssl_engine_);
      ENGINE_cleanup();  // for openssl < 1.1
    }
  }
#endif

#if AKTUALIZR_OPENSSL_NO_ENGINE
  // Loads the private key identified by `key_id` (an uptane/tls key id, same as passed to
  // getItemFullId()) via libp11, for use with the high-level EVP_PKEY signing API.
  // Returns nullptr on failure. Caller owns the result (EVP_PKEY_free()).
  EVP_PKEY *loadPrivateKey(const std::string &key_id) const;
#else
  ENGINE *getEngine() { return ssl_engine_; }
#endif
  std::string getItemFullId(const std::string &id) const { return uri_prefix_ + id; }
  bool readUptanePublicKey(const std::string &uptane_key_id, std::string *key_out);
  bool readTlsCert(const std::string &id, std::string *cert_out) const;
  bool generateUptaneKeyPair(const std::string &uptane_key_id);

 private:
  const boost::filesystem::path module_path_;
  const std::string pass_;
  const std::string label_;
#if !AKTUALIZR_OPENSSL_NO_ENGINE
  ENGINE *ssl_engine_{nullptr};
#endif
  std::string uri_prefix_;
  P11ContextWrapper ctx_;
  P11SlotsWrapper wslots_;

#if !AKTUALIZR_OPENSSL_NO_ENGINE
  static boost::filesystem::path findPkcsLibrary();
#endif
  PKCS11_slot_st *findTokenSlot() const;

  explicit P11Engine(boost::filesystem::path module_path, std::string pass, std::string label);

  friend class P11EngineGuard;
  FRIEND_TEST(crypto, findPkcsLibrary);
};

class P11EngineGuard {
 public:
  explicit P11EngineGuard(boost::filesystem::path module_path, std::string pass, std::string label) {
    if (instance == nullptr) {
      instance = new P11Engine(std::move(module_path), std::move(pass), std::move(label));
    }
    ++ref_counter;
  }

  ~P11EngineGuard() {
    if (ref_counter != 0) {
      --ref_counter;
    }
    if (ref_counter == 0) {
      delete instance;
      instance = nullptr;
    }
  }

  P11EngineGuard(const P11EngineGuard &) = delete;
  P11EngineGuard(P11EngineGuard &&) = delete;
  P11EngineGuard &operator=(const P11EngineGuard &) = delete;
  P11EngineGuard &operator=(P11EngineGuard &&) = delete;

  P11Engine *operator->() const { return instance; }

 private:
  static P11Engine *instance;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
  static int ref_counter;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

#endif
