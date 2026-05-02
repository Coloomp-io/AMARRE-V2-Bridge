#ifndef GCP_CONFIG_H
#define GCP_CONFIG_H

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>

using json = nlohmann::json;

/**
 * Google Cloud Platform configuration manager
 * Loads and manages GCP settings from configuration file
 */
class GcpConfig {
private:
    std::string gcp_project_id_;
    std::string pubsub_topic_id_;
    std::string mqtt_broker_;
    std::string mqtt_topic_;
    int mqtt_qos_ = 1;
    bool use_pubsub_ = true;

public:
    /**
     * Load configuration from JSON file
     * @param config_path - Path to configuration file
     * @return true if successful, false otherwise
     */
    bool load_from_file(const std::string& config_path) {
        try {
            std::cout << "[Config] Loading configuration from: " << config_path << std::endl;
            
            std::ifstream config_file(config_path);
            if (!config_file.is_open()) {
                std::cerr << "[Config] Failed to open configuration file" << std::endl;
                return false;
            }

            json config_json;
            config_file >> config_json;

            // Load GCP settings
            if (config_json.contains("gcp")) {
                auto gcp_config = config_json["gcp"];
                if (gcp_config.contains("project_id")) {
                    gcp_project_id_ = gcp_config["project_id"].get<std::string>();
                }
                if (gcp_config.contains("pubsub_topic")) {
                    pubsub_topic_id_ = gcp_config["pubsub_topic"].get<std::string>();
                }
                if (gcp_config.contains("use_pubsub")) {
                    use_pubsub_ = gcp_config["use_pubsub"].get<bool>();
                }
            }

            // Load MQTT settings
            if (config_json.contains("mqtt")) {
                auto mqtt_config = config_json["mqtt"];
                if (mqtt_config.contains("broker")) {
                    mqtt_broker_ = mqtt_config["broker"].get<std::string>();
                }
                if (mqtt_config.contains("topic")) {
                    mqtt_topic_ = mqtt_config["topic"].get<std::string>();
                }
                if (mqtt_config.contains("qos")) {
                    mqtt_qos_ = mqtt_config["qos"].get<int>();
                }
            }

            std::cout << "[Config] Configuration loaded successfully" << std::endl;
            print_config();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[Config] Error loading configuration: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * Create a default configuration file
     * @param config_path - Path where to create the file
     */
    static bool create_default_config(const std::string& config_path) {
        try {
            json default_config = {
                {
                    "gcp", {
                        {"project_id", "YOUR_GCP_PROJECT_ID"},
                        {"pubsub_topic", "mqtt-messages"},
                        {"use_pubsub", true}
                    }
                },
                {
                    "mqtt", {
                        {"broker", "tcp://localhost:1883"},
                        {"topic", "devices/+/telemetry"},
                        {"qos", 1}
                    }
                }
            };

            std::ofstream config_file(config_path);
            config_file << default_config.dump(4) << std::endl;
            config_file.close();

            std::cout << "[Config] Default configuration created at: " << config_path << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[Config] Error creating default configuration: " << e.what() << std::endl;
            return false;
        }
    }

    // Getters
    std::string get_gcp_project_id() const { return gcp_project_id_; }
    std::string get_pubsub_topic_id() const { return pubsub_topic_id_; }
    std::string get_mqtt_broker() const { return mqtt_broker_; }
    std::string get_mqtt_topic() const { return mqtt_topic_; }
    int get_mqtt_qos() const { return mqtt_qos_; }
    bool get_use_pubsub() const { return use_pubsub_; }

    // Setters
    void set_gcp_project_id(const std::string& id) { gcp_project_id_ = id; }
    void set_pubsub_topic_id(const std::string& topic) { pubsub_topic_id_ = topic; }
    void set_mqtt_broker(const std::string& broker) { mqtt_broker_ = broker; }
    void set_mqtt_topic(const std::string& topic) { mqtt_topic_ = topic; }
    void set_mqtt_qos(int qos) { mqtt_qos_ = qos; }
    void set_use_pubsub(bool use_it) { use_pubsub_ = use_it; }

    /**
     * Print configuration to console
     */
    void print_config() const {
        std::cout << "[Config] Current Configuration:" << std::endl;
        std::cout << "  GCP Project ID: " << gcp_project_id_ << std::endl;
        std::cout << "  Pub/Sub Topic: " << pubsub_topic_id_ << std::endl;
        std::cout << "  Use Pub/Sub: " << (use_pubsub_ ? "Yes" : "No") << std::endl;
        std::cout << "  MQTT Broker: " << mqtt_broker_ << std::endl;
        std::cout << "  MQTT Topic: " << mqtt_topic_ << std::endl;
        std::cout << "  MQTT QoS: " << mqtt_qos_ << std::endl;
    }

    /**
     * Validate configuration
     */
    bool validate() const {
        if (use_pubsub_) {
            if (gcp_project_id_.empty()) {
                std::cerr << "[Config] GCP Project ID is required when using Pub/Sub" << std::endl;
                return false;
            }
            if (gcp_project_id_ == "YOUR_GCP_PROJECT_ID") {
                std::cerr << "[Config] Replace YOUR_GCP_PROJECT_ID before enabling Pub/Sub" << std::endl;
                return false;
            }
            if (pubsub_topic_id_.empty()) {
                std::cerr << "[Config] Pub/Sub Topic ID is required when using Pub/Sub" << std::endl;
                return false;
            }
        }
        
        if (mqtt_broker_.empty()) {
            std::cerr << "[Config] MQTT Broker URL is required" << std::endl;
            return false;
        }
        
        if (mqtt_topic_.empty()) {
            std::cerr << "[Config] MQTT Topic is required" << std::endl;
            return false;
        }

        if (mqtt_qos_ < 0 || mqtt_qos_ > 2) {
            std::cerr << "[Config] MQTT QoS must be 0, 1, or 2" << std::endl;
            return false;
        }

        return true;
    }
};

#endif // GCP_CONFIG_H
