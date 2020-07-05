#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <shaderc/shaderc.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class ShaderManager {
public:
    static ShaderManager& instance();

    std::optional<std::string> loadAndCompileImmediately(const std::string& name);
    const std::vector<uint32_t>& spirv(const std::string& name) const;

    void startFileWatching(unsigned msBetweenPolls, std::function<void()> fileChangeCallback = {});
    void stopFileWatching();

    ShaderManager() = delete;
    ShaderManager(ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;

private:
    explicit ShaderManager(std::string basePath);
    ~ShaderManager() = default;

    std::string resolvePath(const std::string& name) const;
    uint64_t getFileEditTimestamp(const std::string&) const;

    struct ShaderData {
        ShaderData() = default;
        explicit ShaderData(std::string path)
            : filePath(std::move(path))
        {
        }

        std::string filePath {};

        uint64_t lastEditTimestamp { 0 };
        uint32_t currentBinaryVersion { 0 };
        bool lastEditSuccessfullyCompiled { false };
        std::string lastCompileError {};

        std::string glslSource {};
        std::vector<uint32_t> spirvBinary {};
    };

    shaderc_shader_kind shaderKindForPath(const std::string&) const;
    bool compileGlslToSpirv(ShaderData& data) const;

    std::string m_shaderBasePath;
    std::unordered_map<std::string, ShaderData> m_loadedShaders {};

    std::unique_ptr<std::thread> m_fileWatcherThread {};
    mutable std::mutex m_shaderDataMutex {};
    volatile bool m_fileWatchingActive { false };
};
