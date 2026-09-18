#include "sd_stream_server.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <utility>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_http_server.h"

namespace esphome {
namespace sd_stream_server {

static const char *const TAG = "sd_stream_server";

void SdStreamServer::setup() { this->base_->add_handler(this); }

void SdStreamServer::dump_config() {
  char addr_buf[network::USE_ADDRESS_BUFFER_SIZE];
  ESP_LOGCONFIG(TAG, "SD Stream Server:");
  ESP_LOGCONFIG(TAG, "  Address: %s:%u%s", network::get_use_address_to(addr_buf), this->base_->get_port(),
                this->build_prefix_().c_str());
  ESP_LOGCONFIG(TAG, "  Root Path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Chunk Size: %u B", (unsigned) this->chunk_size_);
}

std::string SdStreamServer::build_prefix_() const {
  std::string prefix = this->url_prefix_;
  if (prefix.empty() || prefix.front() != '/')
    prefix.insert(prefix.begin(), '/');
  while (prefix.size() > 1 && prefix.back() == '/')
    prefix.pop_back();
  return prefix;
}

std::string SdStreamServer::resolve_path_(const std::string &url) const {
  const std::string prefix = this->build_prefix_();
  if (url.compare(0, prefix.size(), prefix) != 0)
    return "";

  std::string rel = url.substr(prefix.size());
  // Strip a query string; we take no parameters and a '?' in a FAT path is
  // invalid anyway.
  const size_t query = rel.find('?');
  if (query != std::string::npos)
    rel.erase(query);

  if (rel.empty() || rel == "/")
    return "";  // No listing: the prefix itself addresses nothing.

  // Reject traversal before touching the filesystem. ".." anywhere is enough
  // to refuse — this server addresses a flat set of capture files, so there is
  // no legitimate use for it, and a narrow rule is one we can actually verify.
  if (rel.find("..") != std::string::npos)
    return "";

  if (rel.front() != '/')
    rel.insert(rel.begin(), '/');

  return this->root_path_ + rel;
}

bool SdStreamServer::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() != HTTP_GET)
    return false;
  // url() is deprecated since 2026.3.0 and removed in 2026.9.0.
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const std::string url(request->url_to(url_buf));
  if (url.compare(0, 10, "/file_list") == 0)
    return true;
  const std::string prefix = this->build_prefix_();
  return url.compare(0, prefix.size(), prefix) == 0;
}


void SdStreamServer::walk_(httpd_req_t *req, const std::string &abs, const std::string &rel, bool &first, int depth) {
  // ITERATIVE, NOT RECURSIVE, and that is not a style preference.
  //
  // The ESP-IDF httpd worker task runs on a shallow stack (~4352 bytes) and
  // the FatFS -> SDMMC call chain consumes a large part of it before any of
  // our code runs. A recursive walk adds a vector, several std::strings and a
  // formatting buffer per level, and one extra level of depth is enough to
  // exhaust what is left: measured: listing the card root crashed
  // the board outright where a listing scoped one directory deeper had worked
  // fine.
  //
  // A heap worklist keeps the stack flat at one frame regardless of depth.
  std::vector<std::pair<std::string, std::string>> queue;
  queue.emplace_back(abs, rel);

  while (!queue.empty()) {
    const auto item = queue.back();
    queue.pop_back();
    // Guard against a filesystem loop rather than limiting real nesting.
    if (std::count(item.second.begin(), item.second.end(), '/') > 6)
      continue;

    DIR *dir = opendir(item.first.c_str());
    if (dir == nullptr) {
      ESP_LOGW(TAG, "opendir failed: %s errno=%d (%s)", item.first.c_str(), errno, strerror(errno));
      continue;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
      const std::string name(ent->d_name);
      // Skip every dotted entry. That covers staging directories like
      // .partial/ (in-flight or failed writes, which must never look
      // fetchable) and macOS's .Spotlight-V100 and .fseventsd, which a card
      // picks up from being formatted on a Mac and which are noise to a
      // consumer.
      if (name.empty() || name.front() == '.')
        continue;
      const std::string child_abs = item.first + "/" + name;
      struct stat st;
      if (stat(child_abs.c_str(), &st) != 0)
        continue;
      if (S_ISDIR(st.st_mode)) {
        queue.emplace_back(child_abs, item.second + "/" + name);
        continue;
      }
      char buf[320];
      const int n = snprintf(buf, sizeof(buf), "%s{\"path\":\"%s/%s\",\"size\":%lu}", first ? "" : ",",
                             item.second.c_str(), name.c_str(), (unsigned long) st.st_size);
      if (n > 0 && static_cast<size_t>(n) < sizeof(buf)) {
        httpd_resp_send_chunk(req, buf, n);
        first = false;
      }
    }
    // Closed before the next iteration opens another: each DIR holds a FATFS
    // handle, and a mount's max_files limit is typically a handful.
    closedir(dir);
  }
}

void SdStreamServer::handle_list_(AsyncWebServerRequest *request, const std::string &url) {
  // url_to() returns the path WITHOUT the query string -- its own header says
  // so. Parsing "dir=" out of it silently yields nothing and lists the card
  // root instead, which looks like a working endpoint returning the wrong
  // tree. Use the request's own parameter accessor.
  std::string rel = request->arg("dir");
  if (rel.find("..") != std::string::npos) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }
  while (!rel.empty() && rel.back() == '/')
    rel.pop_back();
  if (!rel.empty() && rel.front() != '/')
    rel.insert(rel.begin(), '/');

  const std::string abs = this->root_path_ + rel;
  httpd_req_t *req = *request;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send_chunk(req, "{\"files\":[", 10);
  bool first = true;
  this->walk_(req, abs, rel, first, 0);
  httpd_resp_send_chunk(req, "]}", 2);
  httpd_resp_send_chunk(req, nullptr, 0);
  ESP_LOGD(TAG, "Listed %s", abs.c_str());
}

void SdStreamServer::handleRequest(AsyncWebServerRequest *request) {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const std::string url(request->url_to(url_buf));

  if (url.compare(0, 10, "/file_list") == 0) {
    this->handle_list_(request, url);
    return;
  }

  const std::string path = this->resolve_path_(url);

  if (path.empty()) {
    ESP_LOGW(TAG, "Rejected: %s", url.c_str());
    request->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  FILE *file = fopen(path.c_str(), "rb");
  if (file == nullptr) {
    // errno is the whole diagnosis here: ENOENT means the puller asked for
    // something absent, EINVAL means the name broke a FATFS limit (long
    // filenames need CONFIG_FATFS_LFN_* enabled). Those need different fixes.
    ESP_LOGW(TAG, "Open failed: path='%s' errno=%d (%s)", path.c_str(), errno, strerror(errno));
    request->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  httpd_req_t *req = *request;
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  // The payload is bytes we do not interpret, so it is streamed verbatim in
  // fixed-size chunks. Peak RAM is one chunk regardless of file size, which is
  // what makes serving a large file safe on a board with 520 KB of SRAM and no
  // PSRAM. ESPHome's own AsyncWebServerResponse cannot do this — it
  // sends a single buffer — so we drop to the ESP-IDF handle underneath, the
  // same way esp32_camera_web_server does.
  std::vector<uint8_t> buf(this->chunk_size_);
  size_t total = 0;
  esp_err_t err = ESP_OK;
  while (true) {
    const size_t got = fread(buf.data(), 1, buf.size(), file);
    if (got == 0)
      break;
    err = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(buf.data()), got);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Send failed after %u B: %s", (unsigned) total, esp_err_to_name(err));
      break;
    }
    total += got;
  }

  const bool read_error = ferror(file) != 0;
  fclose(file);

  if (err == ESP_OK && !read_error) {
    // A zero-length chunk terminates the response. Skipped on failure: leaving
    // it unterminated is what tells the client the transfer was truncated,
    // rather than handing it a short file that looks complete.
    httpd_resp_send_chunk(req, nullptr, 0);
    ESP_LOGD(TAG, "Sent %s (%u B)", path.c_str(), (unsigned) total);
  } else if (read_error) {
    ESP_LOGW(TAG, "Read error on %s after %u B", path.c_str(), (unsigned) total);
  }
}

}  // namespace sd_stream_server
}  // namespace esphome

#endif  // USE_ESP_IDF
