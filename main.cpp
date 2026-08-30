#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <queue>
#include <MQTTAsync.h>
#include <atomic>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "pubsub_subscriber.h"
#include "mqtt_callback.h"
#include "gcp_config.h"
#include "pubsub_publisher.h"

std::atomic_bool g_command_subscriber_stop{false};
std::unique_ptr<PubSubSubscriber> g_command_subscriber;

struct QueuedMqttMessage {
    std::string topic;
    std::string payload;
};

class PublishQueue {
public:
    void push(QueuedMqttMessage message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            queue_.push(std::move(message));
        }
        condition_.notify_one();
    }

    bool wait_pop(QueuedMqttMessage& message) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return false;
        }

        message = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<QueuedMqttMessage> queue_;
    bool closed_ = false;
};

// Global objects
GcpConfig g_config;
std::unique_ptr<PubSubPublisher> g_publisher;
PublishQueue g_publish_queue;
volatile std::sig_atomic_t g_shutdown_requested = 0;

std::string get_env_value(const std::string& name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, name.c_str()) != 0 || value == nullptr) {
        return "";
    }

    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return "";
    }
    return value;
#endif
}

void handle_shutdown_signal(int signal) {
    (void)signal;
    g_shutdown_requested = 1;
}

void pubsub_worker() {
    std::cout << "[PubSubWorker] Publisher worker started" << std::endl;

    QueuedMqttMessage message;
    while (g_publish_queue.wait_pop(message)) {
        if (!g_publisher || !g_config.get_use_pubsub()) {
            continue;
        }

        if (!g_publisher->publish_message(message.topic, message.payload)) {
            std::cerr << "[PubSubWorker] Failed to publish queued message" << std::endl;
        }
    }

    std::cout << "[PubSubWorker] Publisher worker stopped" << std::endl;
}

std::string get_command_topic_from_payload(const std::string& payload) {
    try {
        auto command = nlohmann::json::parse(payload);

        if (command.contains("device") && command["device"].is_string()) {
            auto device_id = command["device"].get<std::string>();
            if (!device_id.empty()) {
                return "devices/" + device_id + "/commands";
            }
        }

        if (command.contains("device_id") && command["device_id"].is_string()) {
            auto device_id = command["device_id"].get<std::string>();
            if (!device_id.empty()) {
                return "devices/" + device_id + "/commands";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[PubSubCommands] Command payload is not JSON: "
                  << e.what() << std::endl;
    }

    std::cout << "[PubSubCommands] Falling back to configured MQTT command topic: "
              << g_config.get_mqtt_command_topic() << std::endl;
    return g_config.get_mqtt_command_topic();
}

bool publish_command_to_mqtt(MqttContext& ctx, const std::string& payload) {
    auto command_topic = get_command_topic_from_payload(payload);
    std::cout << "[PubSubCommands] Publishing command to MQTT topic: "
              << command_topic << std::endl;
    return publish_mqtt_message(ctx, command_topic, payload, g_config.get_mqtt_qos());
};

/**
 * Custom message handler to process incoming MQTT messages
 * Routes messages to Google Cloud Pub/Sub
 */
void handle_mqtt_message(const std::string& topic, const std::string& payload) {
    std::cout << "[APP] Processing message from topic: " << topic << std::endl;
    std::cout << "[APP] Payload length: " << payload.length() << " bytes" << std::endl;
    
    if (g_config.get_use_pubsub() && g_publisher) {
        g_publish_queue.push({topic, payload});
        std::cout << "[APP] Message queued for Pub/Sub publishing" << std::endl;
    }
}

int main() {
    try {
        std::cout << "========================================" << std::endl;
        std::cout << "MQTT to Google Cloud Pub/Sub Bridge" << std::endl;
        std::cout << "========================================" << std::endl;
        std::signal(SIGINT, handle_shutdown_signal);
        std::signal(SIGTERM, handle_shutdown_signal);

        // Step 1: Load configuration
        const auto config_path_env = get_env_value("AMARRE_CONFIG_FILE");
        const std::string config_path = config_path_env.empty() ? "config.json" : config_path_env;
        std::cout << "\n[INIT] Loading configuration..." << std::endl;
        if (!g_config.load_from_file(config_path)) {
            std::cerr << "[ERROR] Failed to load configuration. Creating default config..." << std::endl;
            GcpConfig::create_default_config(config_path);
            std::cout << "[ERROR] Please edit config.json with your GCP credentials and restart" << std::endl;
            return 1;
        }

        // Validate configuration
        if (!g_config.validate()) {
            std::cerr << "[ERROR] Invalid configuration" << std::endl;
            return 1;
        }

        // Step 2: Initialize Google Cloud Pub/Sub Publisher (if enabled)
        std::cout << "\n[INIT] Initializing Google Cloud Pub/Sub..." << std::endl;
        if (g_config.get_use_pubsub()) {
            g_publisher = std::make_unique<PubSubPublisher>(
                g_config.get_gcp_project_id(),
                g_config.get_pubsub_topic_id()
            );
            
            if (!g_publisher->initialize()) {
                std::cerr << "[WARNING] Failed to initialize Pub/Sub. Running in MQTT-only mode." << std::endl;
                g_config.set_use_pubsub(false);
            }
        } else {
            std::cout << "[INIT] Pub/Sub is disabled. Running in MQTT monitor mode." << std::endl;
        }

        // Step 3: Create MQTT context
        MqttContext ctx;
        ctx.message_handler = handle_mqtt_message;

        // Step 4: Create MQTT async client
        std::cout << "\n[INIT] Creating MQTT client..." << std::endl;
        const std::string broker = g_config.get_mqtt_broker();
        const char* CLIENT_ID = "gcp_bridge_listener";
        
        int rc = MQTTAsync_create(&ctx.client, broker.c_str(), CLIENT_ID, 
                                 MQTTCLIENT_PERSISTENCE_NONE, NULL);
        
        if (rc != MQTTASYNC_SUCCESS) {
            std::cerr << "[ERROR] Failed to create MQTT client: " << rc << std::endl;
            return 1;
        }

        // Step 5: Set up callback handlers
        std::cout << "[INIT] Setting up callbacks..." << std::endl;
        MQTTAsync_setCallbacks(ctx.client, &ctx, on_connection_lost, 
                              on_message_arrived, on_delivery_complete);

        // Step 6: Configure connection options
        std::cout << "[INIT] Configuring connection options..." << std::endl;
        MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        conn_opts.context = &ctx;
        conn_opts.onSuccess = on_connect_success;
        conn_opts.onFailure = on_connect_failure;

        const std::string mqtt_username = g_config.get_mqtt_username();
        const std::string mqtt_password_env = g_config.get_mqtt_password_env();
        const std::string mqtt_password = mqtt_username.empty() ? "" : get_env_value(mqtt_password_env);
        if (!mqtt_username.empty()) {
            if (mqtt_password.empty()) {
                std::cerr << "[ERROR] MQTT username is configured, but environment variable "
                          << mqtt_password_env << " is not set or is empty" << std::endl;
                MQTTAsync_destroy(&ctx.client);
                return 1;
            }
            conn_opts.username = mqtt_username.c_str();
            conn_opts.password = mqtt_password.c_str();
            std::cout << "[INIT] MQTT authentication enabled for user: "
                      << mqtt_username << std::endl;
        } else {
            std::cout << "[INIT] MQTT authentication disabled" << std::endl;
        }

        // Step 7: Establish connection
        std::cout << "[INIT] Connecting to MQTT broker at: " << broker << std::endl;
        rc = MQTTAsync_connect(ctx.client, &conn_opts);
        
        if (rc != MQTTASYNC_SUCCESS) {
            std::cerr << "[ERROR] Failed to initiate connection: " << rc << std::endl;
            MQTTAsync_destroy(&ctx.client);
            return 1;
        }

        // Wait for connection
        int retry_count = 0;
        while (!ctx.connected.load() && retry_count < 10) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            retry_count++;
        }

        if (!ctx.connected.load()) {
            std::cerr << "[ERROR] Connection timeout" << std::endl;
            MQTTAsync_destroy(&ctx.client);
            return 1;
        }

        std::cout << "[INIT] Connection established!" << std::endl;

        // Step 8: Subscribe to topics
        const std::string topic = g_config.get_mqtt_topic();
        std::cout << "\n[INIT] Subscribing to topic: " << topic << std::endl;
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.context = &ctx;
        opts.onSuccess = on_subscribe_success;
        opts.onFailure = on_subscribe_failure;
        rc = MQTTAsync_subscribe(ctx.client, topic.c_str(), g_config.get_mqtt_qos(), &opts);
        
        if (rc != MQTTASYNC_SUCCESS) {
            std::cerr << "[ERROR] Failed to subscribe: " << rc << std::endl;
            MQTTAsync_disconnect(ctx.client, NULL);
            MQTTAsync_destroy(&ctx.client);
            return 1;
        }

        retry_count = 0;
        while (!ctx.subscribed.load() && retry_count < 10) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            retry_count++;
        }

        if (!ctx.subscribed.load()) {
            std::cerr << "[ERROR] Subscribe timeout" << std::endl;
            MQTTAsync_disconnect(ctx.client, NULL);
            MQTTAsync_destroy(&ctx.client);
            return 1;
        }

        std::cout << "[INIT] Subscription confirmed!" << std::endl;

        std::thread publisher_thread;
        if (g_config.get_use_pubsub() && g_publisher) {
            publisher_thread = std::thread(pubsub_worker);
        }

        std::thread command_subscriber_thread;
        std::cout << "[INIT] Pub/Sub command bridge: " << (g_config.get_use_pubsub_commands() ? "enabled" : "disabled") << std::endl;
        // Step 8.5: Start Pub/Sub command subscriber if enabled
        if (g_config.get_use_pubsub_commands()) 
            {
                g_command_subscriber = std::make_unique<PubSubSubscriber>(g_config.get_gcp_project_id(), g_config.get_pubsub_command_subscription_id()); // Initialize command subscriber with project ID and subscription ID from config
                command_subscriber_thread = std::thread([&ctx]() {g_command_subscriber->run([&ctx](const std::string& payload) {return publish_command_to_mqtt(ctx, payload);}, g_command_subscriber_stop);}); // Start command subscriber thread with lambda that calls publish_command_to_mqtt
            }
            else 
            {
                std::cout << "[INIT] Pub/Sub command bridge is disabled by config" << std::endl;
            }

        // Step 9: Keep the application running
        std::cout << "\n[RUNTIME] ========================================" << std::endl;
        std::cout << "[RUNTIME] MQTT to Pub/Sub Bridge is running" << std::endl;
        std::cout << "[RUNTIME] Press Ctrl+C to exit..." << std::endl;
        std::cout << "[RUNTIME] ========================================\n" << std::endl;

        // Simulate application runtime
        while (!g_shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Check if client is still connected
            if (!ctx.connected.load()) {
                std::cout << "[WARNING] Client is not connected" << std::endl;
            }
        }

        std::cout << "[APP] Shutting down..." << std::endl;
        g_publish_queue.close();
        if (publisher_thread.joinable()) {
            publisher_thread.join();
        }

        g_command_subscriber_stop = true;

        if (command_subscriber_thread.joinable()) { command_subscriber_thread.join(); }
        
        MQTTAsync_disconnect(ctx.client, NULL);
        MQTTAsync_destroy(&ctx.client);

        return 0;
    } 
    catch (const std::exception& exc) {
        std::cerr << "[FATAL ERROR] Exception: " << exc.what() << std::endl;
        return 1;
    }
}
