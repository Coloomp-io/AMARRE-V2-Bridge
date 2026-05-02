#ifndef PUBSUB_PUBLISHER_H
#define PUBSUB_PUBLISHER_H

#include <google/cloud/pubsub/publisher.h>
#include <google/cloud/pubsub/publisher_options.h>
#include <google/cloud/pubsub/topic.h>
#include <google/cloud/pubsub/message.h>
#include <google/cloud/pubsub/options.h>
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <chrono>
#include <ctime>

namespace gcp = ::google::cloud;

/**
 * Google Cloud Pub/Sub Publisher wrapper class
 * Handles publishing MQTT messages to Google Cloud Pub/Sub topics
 */
class PubSubPublisher {
private:
    std::string project_id_;
    std::string topic_id_;
    std::shared_ptr<gcp::pubsub::Publisher> publisher_;
    bool initialized_ = false;

public:
    /**
     * Constructor
     * @param project_id - Google Cloud project ID
     * @param topic_id - Pub/Sub topic name
     */
    PubSubPublisher(const std::string& project_id, const std::string& topic_id)
        : project_id_(project_id), topic_id_(topic_id), publisher_(nullptr) {}

    /**
     * Initialize the publisher connection
     * @return true if initialization successful, false otherwise
     */
    bool initialize() {
        try {
            std::cout << "[PubSub] Initializing Publisher..." << std::endl;
            std::cout << "[PubSub] Project ID: " << project_id_ << std::endl;
            std::cout << "[PubSub] Topic ID: " << topic_id_ << std::endl;

            // Create publisher connection
            auto topic = gcp::pubsub::Topic(project_id_, topic_id_);
            
            auto options = gcp::Options{}
                .set<gcp::pubsub::MaxBatchBytesOption>(1024 * 1024);  // 1 MB batches

            publisher_ = std::make_shared<gcp::pubsub::Publisher>(
                gcp::pubsub::MakePublisherConnection(topic, options)
            );

            initialized_ = true;
            std::cout << "[PubSub] Publisher initialized successfully" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[PubSub] Failed to initialize publisher: " << e.what() << std::endl;
            initialized_ = false;
            return false;
        }
    }

    /**
     * Publish a message to the Pub/Sub topic
     * @param topic - MQTT topic (will be added as attribute)
     * @param message - Message payload
     * @return true if publish was successful, false otherwise
     */
    bool publish_message(const std::string& mqtt_topic, const std::string& message) {
        if (!initialized_) {
            std::cerr << "[PubSub] Publisher not initialized" << std::endl;
            return false;
        }

        try {
            std::cout << "[PubSub] Publishing message to topic: " << topic_id_ << std::endl;
            std::cout << "[PubSub] MQTT source: " << mqtt_topic << std::endl;
            std::cout << "[PubSub] Message size: " << message.length() << " bytes" << std::endl;

            // Create the message with attributes
            auto pub_message = gcp::pubsub::MessageBuilder()
                .SetData(message)
                .SetAttribute("mqtt_topic", mqtt_topic)
                .SetAttribute("source", "mqtt_bridge")
                .SetAttribute("timestamp", get_timestamp())
                .Build();

            // Publish the message asynchronously
            auto future = publisher_->Publish(pub_message);
            
            // Get the result (this will block until the message is published)
            auto message_id = future.get();

            if (message_id) {
                std::cout << "[PubSub] Message published successfully" << std::endl;
                std::cout << "[PubSub] Message ID: " << message_id.value() << std::endl;
                return true;
            } else {
                std::cerr << "[PubSub] Failed to publish message: " 
                         << message_id.status().message() << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "[PubSub] Error publishing message: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * Check if publisher is initialized and ready
     */
    bool is_initialized() const {
        return initialized_;
    }

    /**
     * Get current timestamp in ISO 8601 format
     */
    static std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm utc_time{};
        char buf[30];
        gmtime_s(&utc_time, &time);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
        return std::string(buf);
    }

    /**
     * Destructor
     */
    ~PubSubPublisher() {
        try {
            // Flush any pending messages before destruction
            if (!publisher_) {
                return;
            }

            publisher_->Flush();
        } catch (const std::exception& e) {
            std::cerr << "[PubSub] Error during cleanup: " << e.what() << std::endl;
        }
    }
};

#endif // PUBSUB_PUBLISHER_H
