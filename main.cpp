#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <MQTTAsync.h>
#include "mqtt_callback.h"
#include "gcp_config.h"
#include "pubsub_publisher.h"

// Global objects
GcpConfig g_config;
std::unique_ptr<PubSubPublisher> g_publisher;

/**
 * Custom message handler to process incoming MQTT messages
 * Routes messages to Google Cloud Pub/Sub
 */
void handle_mqtt_message(const std::string& topic, const std::string& payload) {
    std::cout << "[APP] Processing message from topic: " << topic << std::endl;
    std::cout << "[APP] Payload length: " << payload.length() << " bytes" << std::endl;
    
    // Forward to Google Cloud Pub/Sub if enabled
    if (g_config.get_use_pubsub() && g_publisher) {
        if (!g_publisher->publish_message(topic, payload)) {
            std::cerr << "[APP] Failed to publish message to Pub/Sub" << std::endl;
        }
    }
}

int main() {
    try {
        std::cout << "========================================" << std::endl;
        std::cout << "MQTT to Google Cloud Pub/Sub Bridge" << std::endl;
        std::cout << "========================================" << std::endl;

        // Step 1: Load configuration
        std::cout << "\n[INIT] Loading configuration..." << std::endl;
        if (!g_config.load_from_file("config.json")) {
            std::cerr << "[ERROR] Failed to load configuration. Creating default config..." << std::endl;
            GcpConfig::create_default_config("config.json");
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

        // Step 9: Keep the application running
        std::cout << "\n[RUNTIME] ========================================" << std::endl;
        std::cout << "[RUNTIME] MQTT to Pub/Sub Bridge is running" << std::endl;
        std::cout << "[RUNTIME] Press Ctrl+C to exit..." << std::endl;
        std::cout << "[RUNTIME] ========================================\n" << std::endl;

        // Simulate application runtime
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Check if client is still connected
            if (!ctx.connected.load()) {
                std::cout << "[WARNING] Client is not connected" << std::endl;
            }
        }

        // Cleanup (won't reach here without signal)
        MQTTAsync_disconnect(ctx.client, NULL);
        MQTTAsync_destroy(&ctx.client);

        return 0;
    } 
    catch (const std::exception& exc) {
        std::cerr << "[FATAL ERROR] Exception: " << exc.what() << std::endl;
        return 1;
    }
}
