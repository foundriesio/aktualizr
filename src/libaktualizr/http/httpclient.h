#ifndef HTTPCLIENT_H_
#define HTTPCLIENT_H_

#include <future>
#include <memory>
#include <set>

#include <curl/curl.h>
#include "gtest/gtest_prod.h"
#include "json/json.h"

#include "httpinterface.h"

/**
 * Helper class to manage curl_global_init/curl_global_cleanup calls
 */
class CurlGlobalInitWrapper {
 public:
  CurlGlobalInitWrapper() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobalInitWrapper() { curl_global_cleanup(); }
  CurlGlobalInitWrapper(const CurlGlobalInitWrapper &) = delete;
  CurlGlobalInitWrapper(CurlGlobalInitWrapper &&) = delete;
  CurlGlobalInitWrapper &operator=(const CurlGlobalInitWrapper &) = delete;
  CurlGlobalInitWrapper &operator=(CurlGlobalInitWrapper &&) = delete;
};

class HttpClient : public HttpInterface {
 public:
  explicit HttpClient(const std::vector<std::string> *extra_headers = nullptr,
                      const std::set<std::string> *response_header_names = nullptr);
  explicit HttpClient(const std::string &socket);
  HttpClient(const HttpClient &curl_in);  // non-default!
  ~HttpClient() override;
  HttpClient(HttpClient &&) = default;
  HttpClient &operator=(const HttpClient &) = delete;
  HttpClient &operator=(HttpClient &&) = default;
  HttpResponse get(const std::string &url, int64_t maxsize) override;
  HttpResponse post(const std::string &url, const std::string &content_type, const std::string &data) override;
  HttpResponse post(const std::string &url, const Json::Value &data) override;
  HttpResponse put(const std::string &url, const std::string &content_type, const std::string &data) override;
  HttpResponse put(const std::string &url, const Json::Value &data) override;

  HttpResponse download(const std::string &url, curl_write_callback write_cb, curl_xferinfo_callback progress_cb,
                        void *userp, curl_off_t from) override;
  std::future<HttpResponse> downloadAsync(const std::string &url, curl_write_callback write_cb,
                                          curl_xferinfo_callback progress_cb, void *userp, curl_off_t from,
                                          CurlHandler *easyp) override;
  void setCerts(const std::string &ca, CryptoSource ca_source, const std::string &cert, CryptoSource cert_source,
                const std::string &pkey, CryptoSource pkey_source) override;
  bool updateHeader(const std::string &name, const std::string &value);
  void timeout(int64_t ms);

 private:
  FRIEND_TEST(GetTest, download_speed_limit);

  static const CurlGlobalInitWrapper manageCurlGlobalInit_;
  CURL *curl;
  curl_slist *headers;
  HttpResponse perform(CURL *curl_handler, int retry_times, int64_t size_limit);
  static curl_slist *curl_slist_dup(curl_slist *sl);
  virtual CURL *dupHandle(CURL *const curl_in, const bool using_pkcs11) {
    return Utils::curlDupHandleWrapper(curl_in, using_pkcs11, nullptr);
  }

  std::unique_ptr<TemporaryFile> tls_ca_file;
  std::unique_ptr<TemporaryFile> tls_cert_file;
  std::unique_ptr<TemporaryFile> tls_pkey_file;
  static const int RETRY_TIMES = 2;
  static const long kSpeedLimitTimeInterval = 60L;   // NOLINT(google-runtime-int)
  static const long kSpeedLimitBytesPerSec = 5000L;  // NOLINT(google-runtime-int)

  long speed_limit_time_interval_{kSpeedLimitTimeInterval};                // NOLINT(google-runtime-int)
  long speed_limit_bytes_per_sec_{kSpeedLimitBytesPerSec};                 // NOLINT(google-runtime-int)
  void overrideSpeedLimitParams(long time_interval, long bytes_per_sec) {  // NOLINT(google-runtime-int)
    speed_limit_time_interval_ = time_interval;
    speed_limit_bytes_per_sec_ = bytes_per_sec;
  }
  bool pkcs11_key{false};
  bool pkcs11_cert{false};
  std::set<std::string> response_header_names_;
};

class HttpClientWithShare : public HttpClient {
 public:
  explicit HttpClientWithShare(const std::vector<std::string> *extra_headers = nullptr,
                               const std::set<std::string> *response_header_names = nullptr);
  explicit HttpClientWithShare(const std::string &socket);
  HttpClientWithShare(const HttpClientWithShare &curl_in);  // non-default!
  ~HttpClientWithShare() override;
  HttpClientWithShare(HttpClientWithShare &&) = delete;
  HttpClientWithShare &operator=(const HttpClientWithShare &) = delete;
  HttpClientWithShare &operator=(HttpClientWithShare &&) = delete;

 protected:
  CURL *dupHandle(CURL *const curl_in, const bool using_pkcs11) override {
    return Utils::curlDupHandleWrapper(curl_in, using_pkcs11, share_);
  }
  void initCurlShare();

 private:
  CURLSH *share_{nullptr};
  std::array<std::mutex, CURL_LOCK_DATA_LAST> curl_share_mutexes;
};

#endif
