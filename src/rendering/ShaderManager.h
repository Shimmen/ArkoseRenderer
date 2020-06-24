#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#define NV_EXTENSIONS
#include <shaderc/shaderc.hpp>

class ShaderManager {
public:
    enum class ShaderStatus {
        Good,
        FileNotFound,
        CompileError,
    };

    static ShaderManager& instance();

    ShaderManager() = delete;
    ShaderManager(ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;

    void startFileWatching(unsigned msBetweenPolls, std::function<void()> fileChangeCallback = {});
    void stopFileWatching();

    [[nodiscard]] std::string resolvePath(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> shaderError(const std::string& name) const;

    ShaderStatus loadAndCompileImmediately(const std::string& name);

    const std::vector<uint32_t>& spirv(const std::string& name) const;

private:
    explicit ShaderManager(std::string basePath);
    ~ShaderManager() = default;

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
