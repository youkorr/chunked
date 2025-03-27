#pragma once

#include <string>
#include "esphome/components/web_server_base/web_server_base.h"
#include "../sd_mmc_card/sd_mmc_card.h"
#include "esphome/core/component.h"
#include "esp_http_server.h"

namespace esphome {
namespace box3web {

// Rename the Path class to PathUtils to avoid namespace conflict
class PathUtils {
public:
    static const char separator = '/';
    static std::string file_name(const std::string &path);
    static bool is_absolute(const std::string &path);
    static bool trailing_slash(const std::string &path);
    static std::string join(const std::string &first, const std::string &second);
    static std::string remove_root_path(std::string path, const std::string &root);
    static std::string parent_path(const std::string &path);
};

class Box3Web : public Component {  // Removed AsyncWebHandler for ESP-IDF HTTP server
public:
    Box3Web(web_server_base::WebServerBase *base);
    void setup() override;
    void dump_config() override;
    void set_url_prefix(const std::string &prefix);
    void set_root_path(const std::string &path);
    void set_sd_mmc_card(sd_mmc_card::SdMmc *card);
    void set_deletion_enabled(bool allow);
    void set_download_enabled(bool allow);
    void set_upload_enabled(bool allow);

private:
    web_server_base::WebServerBase *base_{nullptr};
    sd_mmc_card::SdMmc *sd_mmc_card_{nullptr};
    std::string url_prefix_{"box3web"};
    std::string root_path_{"/sdcard"};
    bool deletion_enabled_{true};
    bool download_enabled_{true};
    bool upload_enabled_{true};

    // Internal handler class to manage HTTP requests
    class Handler {
    public:
        Handler(sd_mmc_card::SdMmc *sd_card, const std::string &root_path, const std::string &url_prefix);
        esp_err_t registerHandlers(httpd_handle_t server);

    private:
        sd_mmc_card::SdMmc *sd_mmc_card_;
        std::string root_path_;
        std::string url_prefix_;

        static esp_err_t handleGet(httpd_req_t *req);
        static esp_err_t handlePost(httpd_req_t *req);
        static esp_err_t handlePut(httpd_req_t *req);
        static esp_err_t handleDelete(httpd_req_t *req);
    };

    Handler *handler_{nullptr};
};

} // namespace box3web
} // namespace esphome
