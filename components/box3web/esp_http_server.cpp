#pragma once
#include "box3web.h"
#include "esphome/core/component.h"
#include "esphome/components/network/util.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include <string>
#include <vector>

namespace esphome {
namespace esp_http_server {

class EspHttpServer : public Component {
public:
    EspHttpServer() = default;
    
    void setup() override {
        // Start the HTTP server when the component is set up
        start_server();
    }

    void loop() override {
        // Perform any periodic tasks if needed
    }

    void stop_server() {
        if (server_handle_ != nullptr) {
            httpd_stop(server_handle_);
            server_handle_ = nullptr;
            ESP_LOGI(TAG, "HTTP server stopped");
        }
    }

private:
    static const char* TAG;
    httpd_handle_t server_handle_ = nullptr;

    // Helper function to set content type based on file extension
    static std::string get_content_type(const std::string& filename) {
        if (filename.ends_with(".html")) return "text/html";
        if (filename.ends_with(".css")) return "text/css";
        if (filename.ends_with(".js")) return "application/javascript";
        if (filename.ends_with(".json")) return "application/json";
        if (filename.ends_with(".png")) return "image/png";
        if (filename.ends_with(".jpg") || filename.ends_with(".jpeg")) return "image/jpeg";
        if (filename.ends_with(".gif")) return "image/gif";
        if (filename.ends_with(".svg")) return "image/svg+xml";
        return "application/octet-stream";
    }

    // HTTP GET handler example
    static esp_err_t get_handler(httpd_req_t* req) {
        // Extract the requested file path
        std::string file_path = std::string(req->uri);
        
        // Basic security: prevent directory traversal
        if (file_path.find("..") != std::string::npos) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access denied");
            return ESP_FAIL;
        }

        // Prepend a base path (modify as needed)
        file_path = "/sdcard" + file_path;

        // Open the file
        FILE* file = fopen(file_path.c_str(), "rb");
        if (!file) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }

        // Determine content type
        httpd_resp_set_type(req, get_content_type(file_path).c_str());

        // Stream file in chunks
        char chunk[1024];
        size_t bytes_read;
        while ((bytes_read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
            httpd_resp_send_chunk(req, chunk, bytes_read);
        }

        // End chunked transfer
        httpd_resp_send_chunk(req, nullptr, 0);

        fclose(file);
        return ESP_OK;
    }

    // HTTP POST handler example for file upload
    static esp_err_t post_handler(httpd_req_t* req) {
        char content_type[100];
        size_t content_type_len = httpd_req_get_hdr_value_len(req, "Content-Type");
        
        if (content_type_len > 0) {
            httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));
            
            // Check if it's a multipart form data (file upload)
            if (strstr(content_type, "multipart/form-data") != nullptr) {
                // Implement file upload logic
                // This is a basic example and needs more robust implementation
                char buffer[1024];
                int received = 0;
                FILE* uploaded_file = nullptr;

                while (true) {
                    received = httpd_req_recv(req, buffer, sizeof(buffer));
                    
                    if (received < 0) {
                        // Error in receiving
                        ESP_LOGE(TAG, "File upload receive error");
                        return ESP_FAIL;
                    }
                    
                    if (received == 0) {
                        // Done receiving
                        break;
                    }

                    // Open file for writing on first chunk
                    if (!uploaded_file) {
                        uploaded_file = fopen("/sdcard/uploaded_file.bin", "wb");
                        if (!uploaded_file) {
                            ESP_LOGE(TAG, "Cannot open file for writing");
                            return ESP_FAIL;
                        }
                    }

                    // Write received data
                    fwrite(buffer, 1, received, uploaded_file);
                }

                // Close the file
                if (uploaded_file) {
                    fclose(uploaded_file);
                }

                // Send success response
                httpd_resp_send(req, "File uploaded successfully", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
        }

        // Not a file upload
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    esp_err_t start_server() {
        // Configuration for the HTTP server
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_open_sockets = 7;  // Adjust based on your memory constraints
        config.server_port = 80;       // Customize port if needed

        // Start the server
        esp_err_t result = httpd_start(&server_handle_, &config);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTP server");
            return result;
        }

        // Register URI handlers
        httpd_uri_t get_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = get_handler,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(server_handle_, &get_uri);

        httpd_uri_t post_uri = {
            .uri = "/upload",
            .method = HTTP_POST,
            .handler = post_handler,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(server_handle_, &post_uri);

        ESP_LOGI(TAG, "HTTP server started successfully");
        return ESP_OK;
    }
};

// Define the TAG outside the class
const char* EspHttpServer::TAG = "esp_http_server";

} // namespace esp_http_server
} // namespace esphome
