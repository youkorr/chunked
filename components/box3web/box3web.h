#pragma once

#include <string>
#include "esphome/components/web_server_base/web_server_base.h"
#include "../sd_mmc_card/sd_mmc_card.h"
#include "esphome/core/component.h"
#include "esp_http_server.h"

namespace esphome {
namespace box3web {

class Path {
 public:
  static const char separator = '/';
  static std::string file_name(std::string const &path);
  static bool is_absolute(std::string const &path);
  static bool trailing_slash(std::string const &path);
  static std::string join(std::string const &first, std::string const &second);
  static std::string remove_root_path(std::string path, std::string const &root);
};

class Box3Web : public Component {
 public:
  Box3Web(web_server_base::WebServerBase *base = nullptr);

  void setup() override;
  void dump_config() override;

  void set_url_prefix(std::string const &prefix);
  void set_root_path(std::string const &path);
  void set_sd_mmc_card(sd_mmc_card::SdMmc *card);

  void set_deletion_enabled(bool allow);
  void set_download_enabled(bool allow);
  void set_upload_enabled(bool allow);

 private:
  web_server_base::WebServerBase *base_{nullptr};
  sd_mmc_card::SdMmc *sd_mmc_card_{nullptr};
  httpd_handle_t server_{nullptr};

  std::string url_prefix_{"box3web"};
  std::string root_path_{"/sdcard"};

  bool deletion_enabled_{true};
  bool download_enabled_{true};
  bool upload_enabled_{true};

  // Déclarations des handlers
  static esp_err_t http_get_handler(httpd_req_t *req);
  static esp_err_t http_delete_handler(httpd_req_t *req);
  static esp_err_t http_post_handler(httpd_req_t *req);

  // Méthodes internes
  esp_err_t handle_http_get(httpd_req_t *req);
  esp_err_t handle_http_delete(httpd_req_t *req);
  esp_err_t handle_http_post(httpd_req_t *req);
  esp_err_t send_file_chunked(httpd_req_t *req, const std::string &path);
  esp_err_t send_directory_listing(httpd_req_t *req, const std::string &path);

  void register_handlers();
  std::string build_prefix() const;
  std::string extract_path_from_url(std::string const &url) const;
  std::string build_absolute_path(std::string relative_path) const;
  std::string get_content_type(const std::string &path) const;

  const char *component_source_{nullptr};
};

}  // namespace box3web
}  // namespace esphome
