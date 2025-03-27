#include "box3web.h"
#include "esphome/core/log.h"
#include <cstring>
#include <algorithm>
#include <vector>

namespace esphome {
namespace box3web {

static const char *TAG = "box3web";

// =============================================
// Implémentation des méthodes de la classe Path
// =============================================

std::string Path::file_name(std::string const &path) {
    size_t pos = path.find_last_of(separator);
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

bool Path::is_absolute(std::string const &path) {
    return !path.empty() && path[0] == separator;
}

bool Path::trailing_slash(std::string const &path) {
    return !path.empty() && path.back() == separator;
}

std::string Path::join(std::string const &first, std::string const &second) {
    if (first.empty()) return second;
    if (second.empty()) return first;
    
    std::string result = first;
    if (result.back() == separator) result.pop_back();
    return result + separator + (second[0] == separator ? second.substr(1) : second);
}

std::string Path::remove_root_path(std::string path, std::string const &root) {
    if (path.substr(0, root.length()) == root) {
        path = path.substr(root.length());
    }
    if (!path.empty() && path[0] == separator) {
        path = path.substr(1);
    }
    return path;
}

// ==============================================
// Implémentation des méthodes de la classe Box3Web
// ==============================================

Box3Web::Box3Web(web_server_base::WebServerBase *base) : base_(base) {}

// Setters
void Box3Web::set_sd_mmc_card(sd_mmc_card::SdMmc *card) { sd_mmc_card_ = card; }
void Box3Web::set_url_prefix(std::string const &prefix) { url_prefix_ = prefix; }
void Box3Web::set_root_path(std::string const &path) { root_path_ = path; }
void Box3Web::set_deletion_enabled(bool allow) { deletion_enabled_ = allow; }
void Box3Web::set_download_enabled(bool allow) { download_enabled_ = allow; }
void Box3Web::set_upload_enabled(bool allow) { upload_enabled_ = allow; }

void Box3Web::setup() {
    if (!sd_mmc_card_) {
        ESP_LOGE(TAG, "SD card not initialized");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 10;
    config.stack_size = 10240;
    config.max_open_sockets = 5;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    register_handlers();
    ESP_LOGI(TAG, "HTTP server started with prefix: %s", url_prefix_.c_str());
}

void Box3Web::dump_config() {
    ESP_LOGCONFIG(TAG, "Box3Web Configuration:");
    ESP_LOGCONFIG(TAG, "  URL Prefix: %s", url_prefix_.c_str());
    ESP_LOGCONFIG(TAG, "  Root Path: %s", root_path_.c_str());
    ESP_LOGCONFIG(TAG, "  Deletion Enabled: %s", deletion_enabled_ ? "Yes" : "No");
    ESP_LOGCONFIG(TAG, "  Download Enabled: %s", download_enabled_ ? "Yes" : "No");
    ESP_LOGCONFIG(TAG, "  Upload Enabled: %s", upload_enabled_ ? "Yes" : "No");
}

// Handlers statiques
esp_err_t Box3Web::http_get_handler(httpd_req_t *req) {
    Box3Web* instance = static_cast<Box3Web*>(req->user_ctx);
    return instance->handle_http_get(req);
}

esp_err_t Box3Web::http_delete_handler(httpd_req_t *req) {
    Box3Web* instance = static_cast<Box3Web*>(req->user_ctx);
    return instance->handle_http_delete(req);
}

esp_err_t Box3Web::http_post_handler(httpd_req_t *req) {
    Box3Web* instance = static_cast<Box3Web*>(req->user_ctx);
    return instance->handle_http_post(req);
}

void Box3Web::register_handlers() {
    std::string base_uri = build_prefix() + "/*";

    // Handler GET
    httpd_uri_t get_handler = {
        .uri = base_uri.c_str(),
        .method = HTTP_GET,
        .handler = Box3Web::http_get_handler,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &get_handler);

    // Handler DELETE
    if (deletion_enabled_) {
        httpd_uri_t delete_handler = {
            .uri = base_uri.c_str(),
            .method = HTTP_DELETE,
            .handler = Box3Web::http_delete_handler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &delete_handler);
    }

    // Handler POST (Upload)
    if (upload_enabled_) {
        httpd_uri_t post_handler = {
            .uri = base_uri.c_str(),
            .method = HTTP_POST,
            .handler = Box3Web::http_post_handler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &post_handler);
    }
}

// Méthodes de gestion des requêtes
esp_err_t Box3Web::handle_http_get(httpd_req_t *req) {
    if (!download_enabled_) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Downloads disabled");
    }

    std::string path = extract_path_from_url(req->uri);
    std::string abs_path = build_absolute_path(path);

    if (sd_mmc_card_->is_directory(abs_path.c_str())) {
        return send_directory_listing(req, abs_path);
    }
    return send_file_chunked(req, abs_path);
}

esp_err_t Box3Web::send_file_chunked(httpd_req_t *req, const std::string &path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", path.c_str());
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    // Déterminer la taille du fichier
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Configurer les en-têtes
    httpd_resp_set_type(req, get_content_type(path).c_str());
    httpd_resp_set_hdr(req, "Content-Disposition", 
                      ("inline; filename=\"" + Path::file_name(path) + "\"").c_str());
    httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");

    // Envoyer le fichier par morceaux
    const size_t chunk_size = 4096;
    std::vector<uint8_t> buffer(chunk_size);
    esp_err_t ret = ESP_OK;

    while (true) {
        size_t bytes_read = fread(buffer.data(), 1, chunk_size, file);
        if (bytes_read == 0) break;

        if (httpd_resp_send_chunk(req, (const char*)buffer.data(), bytes_read) != ESP_OK) {
            ESP_LOGE(TAG, "File send failed");
            ret = ESP_FAIL;
            break;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ret;
}

esp_err_t Box3Web::send_directory_listing(httpd_req_t *req, const std::string &path) {
    httpd_resp_set_type(req, "text/html");
    
    // En-tête HTML
    const char* header_fmt = R"(<!DOCTYPE html><html><head>
    <title>Directory: %s</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        table { width: 100%%; border-collapse: collapse; }
        th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
        tr:hover { background-color: #f5f5f5; }
    </style>
    </head><body>
    <h1>Directory: %s</h1>
    <table>
    <tr><th>Name</th><th>Type</th><th>Size</th></tr>)";

    char header[1024];
    snprintf(header, sizeof(header), header_fmt, path.c_str(), path.c_str());
    httpd_resp_send_chunk(req, header, strlen(header));

    // Contenu du répertoire
    auto entries = sd_mmc_card_->list_directory_file_info(path.c_str(), 0);
    for (const auto& entry : entries) {
        char row[512];
        snprintf(row, sizeof(row), 
            "<tr><td><a href='%s%s'>%s</a></td><td>%s</td><td>%s</td></tr>",
            entry.path.c_str(),
            entry.is_directory ? "/" : "",
            Path::file_name(entry.path).c_str(),
            entry.is_directory ? "Directory" : "File",
            entry.is_directory ? "-" : std::to_string(entry.size).c_str());
        httpd_resp_send_chunk(req, row, strlen(row));
    }

    // Pied de page
    const char* footer = "</table></body></html>";
    httpd_resp_send_chunk(req, footer, strlen(footer));
    httpd_resp_send_chunk(req, NULL, 0);
    
    return ESP_OK;
}

esp_err_t Box3Web::handle_http_delete(httpd_req_t *req) {
    std::string path = extract_path_from_url(req->uri);
    std::string abs_path = build_absolute_path(path);

    if (!sd_mmc_card_->exists(abs_path.c_str())) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    if (sd_mmc_card_->is_directory(abs_path.c_str())) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Cannot delete directory");
    }

    if (sd_mmc_card_->delete_file(abs_path.c_str())) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    } else {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete failed");
    }
}

esp_err_t Box3Web::handle_http_post(httpd_req_t *req) {
    if (!upload_enabled_) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Uploads disabled");
    }

    char filename[256];
    if (httpd_req_get_hdr_value_str(req, "Filename", filename, sizeof(filename)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Filename header");
    }

    std::string abs_path = build_absolute_path(filename);
    FILE* file = fopen(abs_path.c_str(), "wb");
    if (!file) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
    }

    char buffer[2048];
    int received;
    while ((received = httpd_req_recv(req, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer, 1, received, file);
    }

    if (received < 0) {
        fclose(file);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
    }

    fclose(file);
    return httpd_resp_sendstr(req, "Upload successful");
}

// Méthodes utilitaires
std::string Box3Web::build_prefix() const {
    std::string prefix = url_prefix_;
    if (prefix.empty()) prefix = "box3web";
    if (prefix[0] != '/') prefix = "/" + prefix;
    if (prefix.back() == '/') prefix.pop_back();
    return prefix;
}

std::string Box3Web::extract_path_from_url(std::string const &url) const {
    std::string prefix = build_prefix();
    if (url.compare(0, prefix.length(), prefix) == 0) {
        std::string path = url.substr(prefix.length());
        if (path.empty()) return "/";
        return path;
    }
    return url;
}

std::string Box3Web::build_absolute_path(std::string relative_path) const {
    if (relative_path.empty() || relative_path == "/") return root_path_;
    if (relative_path[0] == '/') relative_path = relative_path.substr(1);
    return Path::join(root_path_, relative_path);
}

std::string Box3Web::get_content_type(const std::string &path) const {
    static const std::vector<std::pair<std::string, std::string>> extensions = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".mp4", "video/mp4"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".txt", "text/plain"},
        {".xml", "application/xml"}
    };

    for (const auto& ext : extensions) {
        if (path.size() >= ext.first.size() && 
            path.compare(path.size() - ext.first.size(), ext.first.size(), ext.first) == 0) {
            return ext.second;
        }
    }
    return "application/octet-stream";
}

}  // namespace box3web
}  // namespace esphome

