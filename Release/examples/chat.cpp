#include "DataManager.hpp"
#include "LLMProvider.hpp"
#include "SessionManager.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "Usage: chat MODEL MESSAGE [SESSION_ID]\n";
        return 2;
    }

    try
    {
        modelBridge::ModelConfig config{};
        config.configId = "local-model";
        config.model.modelName = argv[1];
        config.model.protocol = modelBridge::ApiProtocol::Ollama;
        config.model.provider = modelBridge::ProviderType::Ollama;
        const auto *url = std::getenv("MODELBRIDGE_OLLAMA_URL");
        config.baseUrl = url ? url : "http://127.0.0.1:11434";
        config.timeoutSeconds = 120;

        auto data = std::make_shared<modelBridge::DataManager>();
        auto sessions = std::make_shared<modelBridge::SessionManager>(data);
        const std::string sessionId = argc == 4 ? argv[3] : sessions->createSession("local-user", config.configId);
        if (argc == 4 && !sessions->restoreSession(sessionId))
            throw std::runtime_error("Session not found: " + sessionId);

        modelBridge::OllamaProvider provider;
        if (!provider.initialize(config))
            throw std::runtime_error(provider.getLastError());
        if (!provider.bindSession(sessions, sessionId))
            throw std::runtime_error("Session not found: " + sessionId);

        std::cout << "session_id=" << sessionId << std::endl;
        const auto result = provider.sendMessage(argv[2]);
        std::cout << result.content << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
