#pragma once

#include "types.h"
#include <google/protobuf/message.h>
#include <google/protobuf/any.pb.h>
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace agent_rpc {
namespace common {

class Serializer {
public:
    virtual ~Serializer() = default;
    
    // Serialize a protobuf message to a byte array
    virtual std::string serialize(const google::protobuf::Message& message) = 0;
    
    virtual bool deserialize(const std::string& data, google::protobuf::Message& message) = 0;
    
    virtual std::string serializeToJson(const google::protobuf::Message& message) = 0;
    
    virtual bool deserializeFromJson(const std::string& json, google::protobuf::Message& message) = 0;
    
    virtual std::string getName() const = 0;
};

class ProtobufBinarySerializer : public Serializer {
public:
    ProtobufBinarySerializer() = default;
    ~ProtobufBinarySerializer() = default;
    
    std::string serialize(const google::protobuf::Message& message) override;
    bool deserialize(const std::string& data, google::protobuf::Message& message) override;
    std::string serializeToJson(const google::protobuf::Message& message) override;
    bool deserializeFromJson(const std::string& json, google::protobuf::Message& message) override;
    std::string getName() const override { return "ProtobufBinary"; }
};

class ProtobufJsonSerializer : public Serializer {
public:
    ProtobufJsonSerializer() = default;
    ~ProtobufJsonSerializer() = default;
    
    std::string serialize(const google::protobuf::Message& message) override;
    bool deserialize(const std::string& data, google::protobuf::Message& message) override;
    std::string serializeToJson(const google::protobuf::Message& message) override;
    bool deserializeFromJson(const std::string& json, google::protobuf::Message& message) override;
    std::string getName() const override { return "ProtobufJson"; }
};

class SerializerFactory {
public:
    enum SerializerType {
        PROTOBUF_BINARY,
        PROTOBUF_JSON
    };
    
    static std::unique_ptr<Serializer> createSerializer(SerializerType type);
    static std::vector<std::string> getAvailableSerializers();
};

// Message wrapper for google::protobuf::Any typed messages
class MessageWrapper {
public:
    MessageWrapper() = default;
    ~MessageWrapper() = default;
    
    template<typename T>
    void wrap(const T& message) {
        any_.PackFrom(message);
    }
    
    template<typename T>
    bool unwrap(T& message) {
        return any_.UnpackTo(&message);
    }
    
    std::string getTypeUrl() const { return any_.type_url(); }
    
    void setTypeUrl(const std::string& type_url) { any_.set_type_url(type_url); }
    
    std::string serialize(Serializer& serializer) const;
    
    bool deserialize(const std::string& data, Serializer& serializer);
    
    const google::protobuf::Any& getAny() const { return any_; }
    google::protobuf::Any& getAny() { return any_; }

private:
    google::protobuf::Any any_;
};

class MessageSerializer {
public:
    static MessageSerializer& getInstance();
    
    void initialize(SerializerFactory::SerializerType type = SerializerFactory::PROTOBUF_BINARY);
    
    std::string serializeMessage(const google::protobuf::Message& message);
    
    bool deserializeMessage(const std::string& data, google::protobuf::Message& message);
    
    std::string serializeToJson(const google::protobuf::Message& message);
    
    bool deserializeFromJson(const std::string& json, google::protobuf::Message& message);
    
    template<typename T>
    MessageWrapper wrapMessage(const T& message) {
        MessageWrapper wrapper;
        wrapper.wrap(message);
        return wrapper;
    }
    
    template<typename T>
    bool unwrapMessage(const MessageWrapper& wrapper, T& message) {
        return wrapper.unwrap(message);
    }
    
    Serializer* getSerializer() { return serializer_.get(); }

private:
    MessageSerializer() = default;
    ~MessageSerializer() = default;
    MessageSerializer(const MessageSerializer&) = delete;
    MessageSerializer& operator=(const MessageSerializer&) = delete;
    
    std::unique_ptr<Serializer> serializer_;
};

// Convenience macros
#define SERIALIZE_MESSAGE(msg) MessageSerializer::getInstance().serializeMessage(msg)
#define DESERIALIZE_MESSAGE(data, msg) MessageSerializer::getInstance().deserializeMessage(data, msg)
#define SERIALIZE_TO_JSON(msg) MessageSerializer::getInstance().serializeToJson(msg)
#define DESERIALIZE_FROM_JSON(json, msg) MessageSerializer::getInstance().deserializeFromJson(json, msg)

} // namespace common
} // namespace agent_rpc
