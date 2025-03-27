#include "box3web.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
#include "esphome/core/helpers.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esphome/core/helpers.h" // For str_replace and other string utilities

namespace esphome {
namespace box3web {

static const char *TAG = "box3web";
static const size_t CHUNK_SIZE = 4096; // Adjust chunk size for better performance

class Box3WebHandler {
public:
    Box3WebHandler(sd_mmc_card::SdMmc *sd_card, const std::string &root_path, const std::string &url_prefix)
        : sd_mmc_card_(sd_card), root_path_(root_path), url_prefix_(url_prefix) {}

    esp_err_t registerHandlers(httpd_handle_t server) {
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
        httpd_uri_t uri_delete = {
            .uri = (url_prefix_ + "/*").c_str(),
            .method = HTTP_DELETE,
            .handler = handleDelete,
            .user_ctx = this
        };
        httpd_uri_t uri_put = {
            .uri = (url_prefix_ + "/*").c_str(),
            .method = HTTP_PUT,
            .handler = handlePut,
            .user_ctx = this
        };

        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_get));
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_post));
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_delete));
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_put));

        return ESP_OK;
    }

private:
    sd_mmc_card::SdMmc *sd_mmc_card_;
    std::string root_path_;
    std::string url_prefix_;

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

    static std::string buildAbsolutePath(const std::string &root_path, const std::string &relative_path) {
        // Implement path joining - you may need to replace this with ESPHome's specific path joining method
        return root_path + "/" + relative_path;
    }

    static std::string getParentPath(const std::string &path) {
        size_t last_sep = path.find_last_of('/');
        return (last_sep != std::string::npos) ? path.substr(0, last_sep) : "";
    }

    static esp_err_t handleGet(httpd_req_t *req) {
        Box3WebHandler *handler = static_cast<Box3WebHandler*>(req->user_ctx);
        std::string url = std::string(req->uri);
        std::string relative_path = url.substr(handler->url_prefix_.length());
        std::string absolute_path = buildAbsolutePath(handler->root_path_, relative_path);

        // Get content type header
        char content_type[64];
        httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));

        // Handle directory listing
        if (handler->sd_mmc_card_->is_directory(absolute_path)) {
            return handleDirectoryListing(req, handler, absolute_path);
        }

        // Handle file download with chunked transfer
        return handleFileDownload(req, handler, absolute_path);
    }

    static esp_err_t handleDirectoryListing(httpd_req_t *req, Box3WebHandler *handler, const std::string &path) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_status(req, "200 OK");

        std::string html_start = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>Directory Listing</title>
                <style>
                    body { font-family: Arial, sans-serif; }
                    table { width: 100%; border-collapse: collapse; }
                    th, td { border: 1px solid #ddd; padding: 8px; }
                </style>
            </head>
            <body>
            <h1>Directory: )";
        html_start += path + "</h1><table><tr><th>Name</th><th>Type</th><th>Size</th></tr>";
        httpd_resp_sendstr_chunk(req, html_start.c_str());

        // Note: You may need to modify this based on the actual SdMmc card API
        auto entries = handler->sd_mmc_card_->list_directory_file_info(path, 0);
        for (const auto &entry : entries) {
            std::string row = "<tr><td>" + entry.path + "</td><td>" + 
                (entry.is_directory ? "Directory" : "File") + "</td><td>" + 
                (entry.is_directory ? "-" : std::to_string(entry.size)) + "</td></tr>";
            httpd_resp_sendstr_chunk(req, row.c_str());
        }

        std::string html_end = "</table></body></html>";
        httpd_resp_sendstr_chunk(req, html_end.c_str());
        httpd_resp_sendstr_chunk(req, nullptr);

        return ESP_OK;
    }

    static esp_err_t handleFileDownload(httpd_req_t *req, Box3WebHandler *handler, const std::string &path) {
        FILE *file = fopen(path.c_str(), "rb");
        if (!file) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
            return ESP_FAIL;
        }

        fseek(file, 0, SEEK_END);
        size_t file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        httpd_resp_set_type(req, getContentType(path).c_str());
        httpd_resp_set_status(req, "200 OK");
        
        httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");

        std::string filename = path.substr(path.find_last_of("/\\") + 1);
        std::string content_disposition = "attachment; filename=\"" + filename + "\"";
        httpd_resp_set_hdr(req, "Content-Disposition", content_disposition.c_str());

        std::vector<char> chunk(CHUNK_SIZE);
        size_t bytes_read;
        while ((bytes_read = fread(chunk.data(), 1, chunk.size(), file)) > 0) {
            httpd_resp_send_chunk(req, chunk.data(), bytes_read);
        }

        httpd_resp_send_chunk(req, nullptr, 0);

        fclose(file);
        return ESP_OK;
    }

    static esp_err_t handlePost(httpd_req_t *req) {
        Box3WebHandler *handler = static_cast<Box3WebHandler*>(req->user_ctx);
        
        // Get content type header
        char content_type[64];
        httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));
        
        // Check for multipart/form-data
        if (strstr(content_type, "multipart/form-data") == nullptr) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Requires multipart/form-data");
            return ESP_FAIL;
        }

        // Note: Full multipart parsing is complex and would require additional libraries
        // This is a placeholder implementation
        httpd_resp_set_status(req, "201 Created");
        httpd_resp_sendstr(req, "File upload not fully implemented");
        return ESP_OK;
    }

    static esp_err_t handleDelete(httpd_req_t *req) {
        Box3WebHandler *handler = static_cast<Box3WebHandler*>(req->user_ctx);
        std::string url = std::string(req->uri);
        std::string relative_path = url.substr(handler->url_prefix_.length());
        std::string absolute_path = buildAbsolutePath(handler->root_path_, relative_path);

        if (!handler->sd_mmc_card_->exists(absolute_path)) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }

        if (handler->sd_mmc_card_->is_directory(absolute_path)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Cannot delete directory");
            return ESP_FAIL;
        }

        // Note: Replace with appropriate SD card file deletion method
        bool deletion_success = false;
        FILE* file = fopen(absolute_path.c_str(), "r");
        if (file) {
            fclose(file);
            deletion_success = (std::remove(absolute_path.c_str()) == 0);
        }

        if (!deletion_success) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
            return ESP_FAIL;
        }

        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_sendstr(req, "");
        return ESP_OK;
    }

    static esp_err_t handlePut(httpd_req_t *req) {
        Box3WebHandler *handler = static_cast<Box3WebHandler*>(req->user_ctx);
        std::string url = std::string(req->uri);
        std::string relative_path = url.substr(handler->url_prefix_.length());
        std::string absolute_path = buildAbsolutePath(handler->root_path_, relative_path);
    
        // Implement file/directory rename or create directory
        bool is_directory = relative_path.back() == '/';
        
        if (is_directory) {
            // Create directory
            // Convert std::string to const char* using c_str()
            if (handler->sd_mmc_card_->create_directory(absolute_path.c_str())) {
                httpd_resp_set_status(req, "201 Created");
                httpd_resp_sendstr(req, "Directory created");
                return ESP_OK;
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
                return ESP_FAIL;
            }
        } else {
            // Rename file
            // Extract new name from headers or request body
            char new_name[256];
            size_t new_name_len = httpd_req_get_hdr_value_len(req, "X-New-Name");
            if (new_name_len > 0) {
                httpd_req_get_hdr_value_str(req, "X-New-Name", new_name, sizeof(new_name));
                std::string new_path = Path::join(Path::parent_path(absolute_path), std::string(new_name));
                
                // Convert std::string to const char* using c_str()
                if (handler->sd_mmc_card_->rename_file(absolute_path.c_str(), new_path.c_str())) {
                    httpd_resp_set_status(req, "200 OK");
                    httpd_resp_sendstr(req, "File renamed");
                    return ESP_OK;
                } else {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to rename file");
                    return ESP_FAIL;
                }
            }
        }
    
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid rename request");
        return ESP_FAIL;
    }
};

// Removed the duplicate Box3Web class definition to prevent redefinition error

} // namespace box3web
} // namespace esphome

