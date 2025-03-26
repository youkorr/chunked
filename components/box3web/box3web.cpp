#include "box3web.h"
#include "esphome/core/log.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <algorithm>
#include <filesystem>

namespace esphome {
namespace box3web {

static const char *TAG = "box3web";

// Implémentation des méthodes de la classe Path
std::string Path::file_name(std::string const &path) {
    size_t last_separator = path.find_last_of(separator);
    return (last_separator != std::string::npos) ? path.substr(last_separator + 1) : path;
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
    if (result.back() == separator) {
        result.pop_back();
    }
    result += separator;
    result += (second[0] == separator ? second.substr(1) : second);
    return result;
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

// Implémentation de la classe Box3Web
Box3Web::Box3Web(web_server_base::WebServerBase *base) : base_(base) {}

void Box3Web::setup() {
    if (base_ && sd_mmc_card_) {
        base_->add_handler(this);
    } else {
        ESP_LOGW(TAG, "Box3Web setup failed: web server or SD card not configured");
    }
}

void Box3Web::dump_config() {
    ESP_LOGCONFIG(TAG, "Box3Web Configuration:");
    ESP_LOGCONFIG(TAG, "  URL Prefix: %s", url_prefix_.c_str());
    ESP_LOGCONFIG(TAG, "  Root Path: %s", root_path_.c_str());
    ESP_LOGCONFIG(TAG, "  Deletion Enabled: %s", deletion_enabled_ ? "Yes" : "No");
    ESP_LOGCONFIG(TAG, "  Download Enabled: %s", download_enabled_ ? "Yes" : "No");
    ESP_LOGCONFIG(TAG, "  Upload Enabled: %s", upload_enabled_ ? "Yes" : "No");
}

void Box3Web::set_url_prefix(std::string const &prefix) {
    url_prefix_ = prefix;
}

void Box3Web::set_root_path(std::string const &path) {
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

bool Box3Web::canHandle(AsyncWebServerRequest *request) {
    std::string url = request->url().c_str();
    return url.compare(0, url_prefix_.length(), url_prefix_) == 0;
}

void Box3Web::handleRequest(AsyncWebServerRequest *request) {
    std::string url = request->url().c_str();
    std::string path = extract_path_from_url(url);

    switch (request->method()) {
        case HTTP_GET:
            handle_get(request);
            break;
        case HTTP_DELETE:
            if (deletion_enabled_) {
                handle_delete(request);
            } else {
                request->send(403, "text/plain", "Deletion not allowed");
            }
            break;
        default:
            request->send(405, "text/plain", "Method Not Allowed");
            break;
    }
}

void Box3Web::handle_get(AsyncWebServerRequest *request) const {
    std::string url = request->url().c_str();
    std::string path = extract_path_from_url(url);

    if (!download_enabled_) {
        request->send(403, "text/plain", "Download not allowed");
        return;
    }

    std::string absolute_path = build_absolute_path(path);
    if (!sd_mmc_card_->exists(absolute_path.c_str())) {
        request->send(404, "text/plain", "File or directory not found");
        return;
    }

    if (sd_mmc_card_->is_directory(absolute_path.c_str())) {
        handle_index(request, absolute_path);
    } else {
        handle_download(request, absolute_path);
    }
}

void Box3Web::handle_index(AsyncWebServerRequest *request, std::string const &path) const {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->printf("<!DOCTYPE html><html><head><title>Directory Listing</title>");
    response->printf("<style>body{font-family:Arial,sans-serif;} table{width:100%%;border-collapse:collapse;}</style>");
    response->printf("</head><body>");
    response->printf("<h1>Directory: %s</h1>", path.c_str());
    response->printf("<table border='1'><tr><th>Name</th><th>Type</th><th>Size</th></tr>");

    auto entries = sd_mmc_card_->list_directory_file_info(path.c_str(), 0);
    for (const auto &info : entries) {
        write_row(response, info);
    }

    response->printf("</table></body></html>");
    request->send(response);
}

void Box3Web::handle_download(AsyncWebServerRequest *request, const std::string &path) const {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        request->send(404, "text/plain", "File not found");
        return;
    }

    AsyncWebServerResponse *response = request->beginResponse(
        200, get_content_type(path).c_str(),
        [file](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t bytesRead = fread(buffer, 1, maxLen, file);
            if (bytesRead == 0) {
                fclose(file);
            }
            return bytesRead;
        }
    );
    request->send(response);
}

void Box3Web::handle_upload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
                            size_t len, bool final) {
    static FILE *file = nullptr;

    if (index == 0) {
        file = fopen(filename.c_str(), "wb");
        if (!file) {
            request->send(500, "text/plain", "Failed to open file for writing");
            return;
        }
    }

    if (file) {
        fwrite(data, 1, len, file);
    }

    if (final) {
        if (file) {
            fclose(file);
            file = nullptr;
        }
        request->send(200, "text/plain", "Upload successful");
    }
}

void Box3Web::write_row(AsyncResponseStream *response, sd_mmc_card::FileInfo const &info) const {
    response->printf("<tr><td>%s</td><td>%s</td><td>%s</td></tr>",
                     info.path.c_str(),
                     info.is_directory ? "Directory" : "File",
                     info.is_directory ? "-" : std::to_string(info.size).c_str());
}

void Box3Web::handle_delete(AsyncWebServerRequest *request) {
    std::string url = request->url().c_str();
    std::string path = extract_path_from_url(url);
    std::string absolute_path = build_absolute_path(path);

    if (!sd_mmc_card_->exists(absolute_path.c_str())) {
        request->send(404, "text/plain", "File not found");
        return;
    }

    if (sd_mmc_card_->is_directory(absolute_path.c_str())) {
        request->send(400, "text/plain", "Cannot delete directory");
        return;
    }

    if (sd_mmc_card_->delete_file(absolute_path.c_str())) {
        request->send(204, "text/plain", "File deleted");
    } else {
        request->send(500, "text/plain", "Failed to delete file");
    }
}

String Box3Web::get_content_type(const std::string &path) const {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css") != std::string::npos) return "text/css";
    if (path.find(".js") != std::string::npos) return "application/javascript";
    if (path.find(".json") != std::string::npos) return "application/json";
    if (path.find(".png") != std::string::npos) return "image/png";
    if (path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) return "image/jpeg";
    if (path.find(".gif") != std::string::npos) return "image/gif";
    if (path.find(".svg") != std::string::npos) return "image/svg+xml";
    if (path.find(".ico") != std::string::npos) return "image/x-icon";
    if (path.find(".mp3") != std::string::npos) return "audio/mpeg";
    if (path.find(".wav") != std::string::npos) return "audio/wav";
    if (path.find(".mp4") != std::string::npos) return "video/mp4";
    if (path.find(".pdf") != std::string::npos) return "application/pdf";
    if (path.find(".zip") != std::string::npos) return "application/zip";
    if (path.find(".txt") != std::string::npos) return "text/plain";
    if (path.find(".xml") != std::string::npos) return "application/xml";
    return "application/octet-stream";
}

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
        return url.substr(prefix.length());
    }
    return url;
}

std::string Box3Web::build_absolute_path(std::string relative_path) const {
    if (!relative_path.empty() && relative_path[0] == '/') {
        relative_path = relative_path.substr(1);
    }
    return Path::join(root_path_, relative_path);
}

}  // namespace box3web
}  // namespace esphome

