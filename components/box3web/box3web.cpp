#include "box3web.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
#include "esphome/core/helpers.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"

namespace esphome {
namespace box3web {

static const char *TAG = "box3web";
static const size_t CHUNK_SIZE = 4096;

// Utility function for checking file extensions
static bool endsWith(const std::string &str, const std::string &suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

// PathUtils implementation
std::string PathUtils::file_name(const std::string &path) {
    size_t last_sep = path.find_last_of('/');
    return (last_sep == std::string::npos) ? path : path.substr(last_sep + 1);
}

bool PathUtils::is_absolute(const std::string &path) {
    return !path.empty() && path[0] == '/';
}

bool PathUtils::trailing_slash(const std::string &path) {
    return !path.empty() && path.back() == '/';
}

std::string PathUtils::join(const std::string &first, const std::string &second) {
    if (first.empty()) return second;
    if (second.empty()) return first;
    
    if (first.back() == '/' && second.front() == '/') {
        return first + second.substr(1);
    }
    if (first.back() == '/' || second.front() == '/') {
        return first + second;
    }
    return first + "/" + second;
}

std::string PathUtils::remove_root_path(std::string path, const std::string &root) {
    if (path.substr(0, root.length()) == root) {
        path = path.substr(root.length());
    }
    // Remove leading slash if present
    if (!path.empty() && path[0] == '/') {
        path = path.substr(1);
    }
    return path;
}

std::string PathUtils::parent_path(const std::string &path) {
    size_t last_sep = path.find_last_of('/');
    if (last_sep == std::string::npos) return "";
    return path.substr(0, last_sep + 1);
}

// Box3Web implementation
Box3Web::Box3Web(web_server_base::WebServerBase *base) : base_(base) {}

void Box3Web::setup() {
    // This is a placeholder. You'll need to modify this based on how 
    // you can access the actual HTTP server handle in ESPHome
    
    // Alternative approaches:
    // 1. Check if WebServerBase has a method to get the server handle
    // 2. If not, you might need to modify the web server component 
    //    to expose the HTTP server handle
    // 3. Or create a separate method in WebServerBase to register this handler
    
    ESP_LOGE(TAG, "Box3Web setup: HTTP server handler registration not implemented");
    
    // Create handler with SD card, root path, and URL prefix
    handler_ = new Handler(sd_mmc_card_, root_path_, url_prefix_);
    
    // TODO: Implement proper handler registration
    // This might require modifications to the WebServerBase class
    // or a different approach to registering HTTP handlers
}

void Box3Web::dump_config() {
    ESP_LOGCONFIG(TAG, "Box3Web:");
    ESP_LOGCONFIG(TAG, "  URL Prefix: %s", url_prefix_.c_str());
    ESP_LOGCONFIG(TAG, "  Root Path: %s", root_path_.c_str());
}

void Box3Web::set_url_prefix(const std::string &prefix) { 
    url_prefix_ = prefix; 
}

void Box3Web::set_root_path(const std::string &path) { 
    root_path_ = path; 
}

void Box3Web::set_sd_mmc_card(sd_mmc_card::SdMmc *card) { 
    sd_mmc_card_ = card; 
}

void Box3Web::set_deletion_enabled(bool allow) { 
    deletion_enabled_ = allow; 
}

void Box3Web::set_download_enabled(bool allow) { 
    download_enabled_ = allow; 
}

void Box3Web::set_upload_enabled(bool allow) { 
    upload_enabled_ = allow; 
}

// Handler implementation
Box3Web::Handler::Handler(sd_mmc_card::SdMmc *sd_card, const std::string &root_path, const std::string &url_prefix)
    : sd_mmc_card_(sd_card), root_path_(root_path), url_prefix_(url_prefix) {}

esp_err_t Box3Web::Handler::registerHandlers(httpd_handle_t server) {
    httpd_uri_t uri_get = {
        .uri = (url_prefix_ + "/*").c_str(),
        .method = HTTP_GET,
        .handler = handleGet,
        .user_ctx = this
    };
    httpd_uri_t uri_post = {
        .uri = (url_prefix_ + "/*").c_str(),
        .method = HTTP_POST,
        .handler = handlePost,
        .user_ctx = this
    };
    httpd_uri_t uri_put = {
        .uri = (url_prefix_ + "/*").c_str(),
        .method = HTTP_PUT,
        .handler = handlePut,
        .user_ctx = this
    };
    httpd_uri_t uri_delete = {
        .uri = (url_prefix_ + "/*").c_str(),
        .method = HTTP_DELETE,
        .handler = handleDelete,
        .user_ctx = this
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_put));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_delete));

    return ESP_OK;
}

static std::string buildAbsolutePath(const std::string &root_path, const std::string &relative_path) {
    return PathUtils::join(root_path, relative_path);
}

static std::string getContentType(const std::string &path) {
    if (endsWith(path, ".html")) return "text/html";
    else if (endsWith(path, ".css")) return "text/css";
    else if (endsWith(path, ".js")) return "application/javascript";
    else if (endsWith(path, ".json")) return "application/json";
    else if (endsWith(path, ".png")) return "image/png";
    else if (endsWith(path, ".jpg") || endsWith(path, ".jpeg")) return "image/jpeg";
    else if (endsWith(path, ".gif")) return "image/gif";
    else if (endsWith(path, ".svg")) return "image/svg+xml";
    else if (endsWith(path, ".ico")) return "image/x-icon";
    else if (endsWith(path, ".mp3")) return "audio/mpeg";
    else if (endsWith(path, ".wav")) return "audio/wav";
    else if (endsWith(path, ".mp4")) return "video/mp4";
    else if (endsWith(path, ".pdf")) return "application/pdf";
    else if (endsWith(path, ".zip")) return "application/zip";
    else if (endsWith(path, ".txt")) return "text/plain";
    else if (endsWith(path, ".xml")) return "application/xml";
    return "application/octet-stream";
}

esp_err_t Box3Web::Handler::handleGet(httpd_req_t *req) {
    Handler *handler = static_cast<Handler*>(req->user_ctx);
    std::string url = std::string(req->uri);
    std::string relative_path = url.substr(handler->url_prefix_.length());
    std::string absolute_path = buildAbsolutePath(handler->root_path_, relative_path);

    // Implement file or directory handling logic here
    // This is a simplified version, you'll want to add more robust error handling
    
    FILE *file = fopen(absolute_path.c_str(), "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // Set content type based on file extension
    httpd_resp_set_type(req, getContentType(absolute_path).c_str());

    // Stream file in chunks
    std::vector<char> chunk(CHUNK_SIZE);
    size_t bytes_read;
    while ((bytes_read = fread(chunk.data(), 1, chunk.size(), file)) > 0) {
        httpd_resp_send_chunk(req, chunk.data(), bytes_read);
    }

    // End chunked transfer
    httpd_resp_send_chunk(req, nullptr, 0);

    fclose(file);
    return ESP_OK;
}

esp_err_t Box3Web::Handler::handlePost(httpd_req_t *req) {
    // Implement file upload logic
    httpd_resp_send_err(req, HTTPD_501_METHOD_NOT_IMPLEMENTED, "File upload not implemented");
    return ESP_FAIL;
}

esp_err_t Box3Web::Handler::handlePut(httpd_req_t *req) {
    // Implement file/directory creation or rename logic
    httpd_resp_send_err(req, HTTPD_501_METHOD_NOT_IMPLEMENTED, "File/directory creation not implemented");
    return ESP_FAIL;
}

esp_err_t Box3Web::Handler::handleDelete(httpd_req_t *req) {
    // Implement file deletion logic
    httpd_resp_send_err(req, HTTPD_501_METHOD_NOT_IMPLEMENTED, "File deletion not implemented");
    return ESP_FAIL;
}

} // namespace box3web
} // namespace esphome

