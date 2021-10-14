#pragma once

#include "rendering/Shader.h"
#include "utility/Badge.h"
#include <functional>
#include <mutex>
#include <optional>
#include <shaderc/shaderc.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Backend;

class ShaderManager {
public:
    static ShaderManager& instance();

    using SpirvData = std::vector<uint32_t>;

    std::string resolveGlslPath(const std::string& name) const;

    std::string createShaderIdentifier(const ShaderFile&) const;

    std::string resolveSpirvPath(const ShaderFile&) const;
    std::string resolveSpirvAssemblyPath(const ShaderFile&) const;

    std::optional<std::string> loadAndCompileImmediately(const ShaderFile&);

    const SpirvData& spirv(const ShaderFile&) const;

    using FilesChangedCallback = std::function<void(const std::vector<std::string>&)>;
    void startFileWatching(unsigned msBetweenPolls, FilesChangedCallback filesChangedCallback = {});
    void stopFileWatching();

    ShaderManager() = delete;
    ShaderManager(ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;

private:
    explicit ShaderManager(std::string basePath);
    ~ShaderManager() = default;

    struct CompiledShader {
        CompiledShader() = default;
        explicit CompiledShader(ShaderManager&, const ShaderFile&, std::string resolvedPath);

        bool tryLoadingFromBinaryCache();
        bool recompile();

        uint64_t findLatestEditTimestampInIncludeTree(bool scanForNewIncludes = false);
        std::vector<std::string> findAllIncludedFiles() const;

        const ShaderManager& shaderManager;

        const ShaderFile& shaderFile;
        std::string resolvedFilePath {};
        std::vector<std::string> includedFilePaths {};

        uint64_t lastEditTimestamp { 0 };
        uint64_t compiledTimestamp { 0 };

        SpirvData currentSpirvBinary {};
        std::string lastCompileError {};
    };

    std::string m_shaderBasePath;
    std::unordered_map<std::string, std::unique_ptr<CompiledShader>> m_compiledShaders {};

    std::unique_ptr<std::thread> m_fileWatcherThread {};
    mutable std::mutex m_shaderDataMutex {};
    volatile bool m_fileWatchingActive { false };
};
