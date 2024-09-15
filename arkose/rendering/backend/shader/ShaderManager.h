#pragma once

#include "rendering/backend/shader/NamedConstant.h"
#include "rendering/backend/shader/NamedConstantLookup.h"
#include "rendering/backend/shader/Shader.h"
#include "core/Badge.h"
#include "core/Types.h"
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Backend;

class ShaderManager {
public:
    static ShaderManager& instance();

    using SpirvData = std::vector<u32>;
    using DXILData = std::vector<u8>;

    std::string resolveSourceFilePath(std::string const& name) const;

    std::string createShaderIdentifier(const ShaderFile&) const;

    bool usingDebugShaders() const;
    char const* currentCachePath() const;

    std::string resolveDxilPath(ShaderFile const&) const;
    std::string resolveSpirvPath(ShaderFile const&) const;
    std::string resolveSpirvAssemblyPath(ShaderFile const&) const;
    std::string resolveMetadataPath(ShaderFile const&) const;
    std::string resolveHlslPath(ShaderFile const&) const;

    void registerShaderFile(ShaderFile const&);

    SpirvData const& spirv(ShaderFile const&) const;
    DXILData const& dxil(ShaderFile const&) const;

    NamedConstantLookup mergeNamedConstants(Shader const&) const;
    bool hasCompatibleNamedConstants(std::vector<ShaderFile> const&, std::vector<NamedConstant>& outMergedConstants) const;

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
        CompiledShader(ShaderManager&, const ShaderFile&, std::string resolvedPath);

        enum class TargetType {
            Spirv,
            DXIL,
        };

        bool tryLoadingFromBinaryCache(TargetType);
        void compileWithRetry(TargetType);

        bool compile(TargetType);
        bool recompile();

        bool collectNamedConstants();

        void writeShaderMetadataFile() const;
        bool readShaderMetadataFile();

        uint64_t findLatestEditTimestampInIncludeTree(bool scanForNewIncludes = false);
        std::vector<std::string> findAllIncludedFiles() const;

        std::string_view findIncludedPathFromShaderCodeLine(std::string_view line, bool& outIsRelative) const;

        const ShaderManager& shaderManager;

        ShaderFile shaderFile;
        std::string resolvedFilePath {};
        std::vector<std::string> includedFilePaths {};

        uint64_t lastEditTimestamp { 0 };
        uint64_t compiledTimestamp { 0 };

        enum class SourceType {
            Unknown,
            GLSL,
            HLSL,
        } sourceType { SourceType::Unknown };

        SpirvData currentSpirvBinary {};
        DXILData currentDxilBinary {};

        std::string lastCompileError {};

        std::vector<NamedConstant> namedConstants {};
    };

    std::string m_shaderBasePath;
    std::unordered_map<std::string, std::unique_ptr<CompiledShader>> m_compiledShaders {};

    std::unique_ptr<std::thread> m_fileWatcherThread {};
    mutable std::mutex m_shaderDataMutex {};
    std::atomic_bool m_fileWatchingActive { false };
};
