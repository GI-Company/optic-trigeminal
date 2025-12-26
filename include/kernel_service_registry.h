#pragma once
#include <unordered_map>
#include <vector>
#include <stdexcept>

enum class KernelService {
    PERCEPTION,
    INTENT,
    REASONING,
    GOVERNANCE,
    MEMORY,
    DECODER,
    IO
};

class KernelServiceRegistry {
public:
    void register_service(KernelService type, void* instance) {
        if (!instance) {
            throw std::runtime_error("Null service registration");
        }
        services[type].push_back(instance);
    }

    const std::vector<void*>& get_services(KernelService type) const {
        auto it = services.find(type);
        if (it == services.end()) {
            static std::vector<void*> empty;
            return empty;
        }
        return it->second;
    }

    void verify_required_services() const {
        require(KernelService::GOVERNANCE);
        require(KernelService::MEMORY);
        require(KernelService::INTENT);
        require(KernelService::REASONING);
        require(KernelService::DECODER);
    }

private:
    std::unordered_map<KernelService, std::vector<void*>> services;

    void require(KernelService type) const {
        auto it = services.find(type);
        if (it == services.end() || it->second.empty()) {
            throw std::runtime_error("Missing required kernel service");
        }
    }
};