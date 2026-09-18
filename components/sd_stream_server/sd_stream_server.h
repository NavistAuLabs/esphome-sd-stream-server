#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <string>
#include <vector>

#ifdef USE_ESP_IDF
#include "esp_http_server.h"
#endif

namespace esphome {
namespace sd_stream_server {

/// Read-only HTTP file server that streams from an already-mounted SD card.
///
/// Serves GET <url_prefix>/<path> by streaming <root_path>/<path> back in
/// fixed-size chunks, plus a JSON index at GET /file_list?dir=<path>. There is
/// no upload, no deletion, no MIME sniffing and no browsable HTML — a
/// scheduled puller knows what it is asking for, and every one of those
/// features is surface that would have to be kept working.
///
/// Registering through WebServerBase::add_handler() means whatever auth
/// web_server is configured with applies before this handler is reached.
/// There is no separate credential here on purpose.
class SdStreamServer : public Component, public AsyncWebHandler {
 public:
  explicit SdStreamServer(web_server_base::WebServerBase *base) : base_(base) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

  /// Streaming means the response is written incrementally rather than handed
  /// over as one buffer, so the framework must not treat this handler as
  /// trivially satisfiable.
  bool isRequestHandlerTrivial() const override { return false; }

  void set_url_prefix(const std::string &prefix) { this->url_prefix_ = prefix; }
  void set_root_path(const std::string &path) { this->root_path_ = path; }
  void set_chunk_size(size_t size) { this->chunk_size_ = size; }

 protected:
  /// "/" + url_prefix, e.g. "/file". Computed per call rather than cached:
  /// it is a handful of bytes and cheaper than another member to keep in sync.
  std::string build_prefix_() const;

  /// Map a request URL to an absolute VFS path, or return an empty string if
  /// the URL escapes root_path_. Traversal rejection lives here so there is
  /// exactly one place to audit it.
  std::string resolve_path_(const std::string &url) const;

  /// GET <list_prefix>?dir=<path> -> {"files":[{"path":..,"size":..}]}
  ///
  /// A consumer cannot enumerate what to fetch otherwise: this server has no
  /// directory browsing, deliberately, so a machine-readable index is the only
  /// way to discover what is on the card.
  void handle_list_(AsyncWebServerRequest *request, const std::string &url);

  /// Recurse, emitting one JSON object per file. Streams as it walks rather
  /// than building the array in memory, for the same reason downloads stream.
  void walk_(httpd_req_t *req, const std::string &abs, const std::string &rel, bool &first, int depth);

  web_server_base::WebServerBase *base_;
  std::string url_prefix_;
  std::string root_path_;
  size_t chunk_size_{4096};
};

}  // namespace sd_stream_server
}  // namespace esphome
