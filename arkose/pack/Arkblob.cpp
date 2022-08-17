#include "Arkblob.h"

#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Image.h"
#include <zstd/zstd.h>

const char* Arkblob::fileExtensionForType(Type type)
{
    switch (type) {
    case Type::Image:
        return "arkimg";
    case Type::Material:
        return "arkmat";
    case Type::Mesh:
        return "arkmsh";
    case Type::Scene:
        return "arkscn";
    }

    ASSERT_NOT_REACHED();
}

Arkblob::Type Arkblob::typeFromFilename(std::string_view filePath)
{
    if (filePath.ends_with(fileExtensionForType(Type::Image))) {
        return Type::Image;
    }

    if (filePath.ends_with(fileExtensionForType(Type::Material))) {
        return Type::Material;
    }

    if (filePath.ends_with(fileExtensionForType(Type::Mesh))) {
        return Type::Mesh;
    }

    if (filePath.ends_with(fileExtensionForType(Type::Scene))) {
        return Type::Scene;
    }

    ARKOSE_LOG_FATAL("Can't derive arkblob type from file path '{}'", filePath);
}

bool Arkblob::readFromFile(std::string_view filePath)
{
    SCOPED_PROFILE_ZONE();

    Type derivedType = typeFromFilename(filePath);
    std::string filePathWithExtension = std::string(filePath);

    std::ifstream fileStream;
    {
        SCOPED_PROFILE_ZONE_NAMED("Opening file stream");
        fileStream.open(filePathWithExtension, std::ios::in | std::ios::binary);
    }

    if (!fileStream.is_open()) {
        ARKOSE_LOG(Error, "Could not open file '{}' for reading arkblob", filePathWithExtension);
        return false;
    }

    if (!readHeader(fileStream)) {
        ARKOSE_LOG(Error, "Could not read arkblob header in file '{}'. Is it a valid arkblob?", filePathWithExtension);
        return false;
    }

    std::vector<char> compressedBlob {};
    compressedBlob.resize(m_compressedSize);
    fileStream.read(compressedBlob.data(), compressedBlob.size());

    m_blob.resize(m_uncompressedSize);

    size_t decompressedSizeOrErrorCode = -1;
    {
        SCOPED_PROFILE_ZONE_NAMED("Decompressing");
        decompressedSizeOrErrorCode = ZSTD_decompress(m_blob.data(), m_blob.size(),
                                                      compressedBlob.data(), compressedBlob.size());
    }

    if (ZSTD_isError(decompressedSizeOrErrorCode)) {
        const char* errorName = ZSTD_getErrorName(decompressedSizeOrErrorCode);
        ARKOSE_LOG(Error, "Failed to decompress arkblob. Reason: {}", errorName);
        return false;
    }

    size_t decompressedSize = decompressedSizeOrErrorCode;
    if (decompressedSize != m_uncompressedSize) {
        ARKOSE_LOG(Error, "Decompressed size does not match uncompressed size in arkblob: {} vs {}", decompressedSize, m_uncompressedSize);
        return false;
    }

    return true;
}

bool Arkblob::writeToFile(std::string_view filePath) const
{
    SCOPED_PROFILE_ZONE();

    const char* extension = fileExtensionForType(m_type);
    std::string filePathWithExtension = filePath.ends_with(extension)
        ? std::string(filePath)
        : std::string(filePath) + "." + extension;
    
    FileIO::ensureDirectoryForFile(filePathWithExtension);

    std::ofstream fileStream;
    fileStream.open(filePathWithExtension, std::ios::out | std::ios::trunc | std::ios::binary);

    if (!fileStream.is_open() || !fileStream.good()) {
        ARKOSE_LOG(Error, "Could not create file '{}' for writing arkblob", filePathWithExtension);
        return false;
    }

    // TODO: Hmm maybe just ensure it has to be done before..
    ARKOSE_ASSERT(m_blobIsCompressed);

    writeHeader(fileStream);

    const char* blobData = reinterpret_cast<const char*>(m_blob.data());
    fileStream.write(blobData, m_blob.size());

    fileStream.close();
    return true;
}

void Arkblob::writeHeader(std::ostream& stream) const
{
    SCOPED_PROFILE_ZONE();

    // Write header magic value without string terminator
    stream.write(HeaderMagic, sizeof(HeaderMagic) - 1);

    auto writeUint32 = [&stream](uint32_t value) {
        stream.write(reinterpret_cast<char*>(&value), sizeof(uint32_t));
    };

    uint32_t type = static_cast<uint32_t>(m_type);
    writeUint32(type);

    ARKOSE_ASSERT(m_compressedSize > 0);
    ARKOSE_ASSERT(m_uncompressedSize > 0);
    ARKOSE_ASSERT(m_uncompressedSize >= m_compressedSize);

    writeUint32(m_compressedSize);
    writeUint32(m_uncompressedSize);
}

bool Arkblob::readHeader(std::istream& stream)
{
    SCOPED_PROFILE_ZONE();

    // TODO: This needs way more validation code!

    // Validate header magic value / string
    char magicString[sizeof(HeaderMagic)] = { 0 };
    stream.read(magicString, sizeof(HeaderMagic) - 1);
    if (std::strcmp(HeaderMagic, magicString) != 0) {
        ARKOSE_LOG(Error, "Invalid header magic string '{}'; expected '{}'.", magicString, HeaderMagic);
        return false;
    }

    auto readUint32 = [&stream]() -> uint32_t {
        char fourBytes[4];
        stream.read(fourBytes, sizeof(fourBytes));
        return *reinterpret_cast<uint32_t*>(fourBytes);
    };

    uint32_t typeVal = readUint32();
    if (typeVal >= static_cast<uint32_t>(Type::__Count)) {
        ARKOSE_LOG(Error, "Invalid arkblob type ({}).", typeVal);
        return false;
    }
    m_type = static_cast<Type>(typeVal);

    m_compressedSize = readUint32();
    m_uncompressedSize = readUint32();

    if (m_compressedSize == 0 || m_uncompressedSize == 0 || m_compressedSize > m_uncompressedSize) {
        ARKOSE_LOG(Error, "Invalid arkblob sizes in file (compressed={}, uncompressed={}).", m_compressedSize, m_uncompressedSize);
        return false;
    }

    return true;
}

std::unique_ptr<Arkblob> Arkblob::makeImageBlob(Image const& image)
{
    SCOPED_PROFILE_ZONE();

    auto imageBlob = std::make_unique<Arkblob>();
    imageBlob->encodeImage(image);

    if (imageBlob->compressBlob()) {
        return imageBlob;
    } else {
        return nullptr;
    }
}

std::unique_ptr<Image> Arkblob::readImageFromBlob(std::string_view filePath)
{
    SCOPED_PROFILE_ZONE();

    auto arkblob = std::make_unique<Arkblob>();
    
    if (!arkblob->readFromFile(filePath)) {
        ARKOSE_LOG(Error, "Could not read arkblob from file '{}'", filePath);
        return nullptr;
    }

    if (!arkblob->isImage()) {
        ARKOSE_LOG(Error, "Arkblob '{}' is not an image", filePath);
        return nullptr;
    }

    return arkblob->decodeImage();
}

void Arkblob::encodeImage(Image const& image)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(isEmpty());
    m_type = Type::Image;

    // TODO: Use something that is safe and works accross machines!

    size_t contentSize = sizeof(Image::Info) + image.dataSize();
    m_blob.resize(contentSize);

    uint8_t* cursor = m_blob.data();
    auto pushData = [&](const uint8_t* data, size_t size) {
        ARKOSE_ASSERT(cursor + size <= m_blob.data() + m_blob.size());
        std::memcpy(cursor, data, size);
        cursor += size;
    };

    pushData(reinterpret_cast<const uint8_t*>(&image.info()), sizeof(Image::Info));
    pushData(image.data(), image.dataSize());
}

std::unique_ptr<Image> Arkblob::decodeImage()
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(isImage());

    // TODO: Use something that is safe and works accross machines!

    uint8_t* cursor = m_blob.data();
    size_t remainingSize = m_blob.size();

    Image::Info imageInfo = *reinterpret_cast<Image::Info*>(cursor);
    cursor += sizeof(Image::Info);
    remainingSize -= sizeof(Image::Info);

    std::vector<uint8_t> imageData {};
    imageData.resize(remainingSize);
    std::memcpy(imageData.data(), cursor, remainingSize);

    return std::make_unique<Image>(imageInfo, imageData);
}

bool Arkblob::isEmpty() const
{
    bool empty = type() == Type::Empty;
    ARKOSE_ASSERT(!empty || m_blob.size() == 0);
    return empty;
}

bool Arkblob::compressBlob()
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(!m_blobIsCompressed);

    size_t maxCompressedSize = ZSTD_compressBound(m_blob.size());
    std::vector<uint8_t> compressedBlob {};
    compressedBlob.resize(maxCompressedSize);

    // Trade-off between time and savings.. this seems to be good for our use case
    constexpr int compressionLevel = 10;

    size_t compressedSizeOrErrorCode = ZSTD_compress(compressedBlob.data(), compressedBlob.size(), m_blob.data(), m_blob.size(), compressionLevel);

    if (ZSTD_isError(compressedSizeOrErrorCode)) {
        const char* errorName = ZSTD_getErrorName(compressedSizeOrErrorCode);
        ARKOSE_LOG(Error, "Failed to compress arkblob. Reason: {}", errorName);
        return false;
    }

    size_t compressedSize = compressedSizeOrErrorCode;
    compressedBlob.resize(compressedSize);

    m_uncompressedSize = static_cast<uint32_t>(m_blob.size());
    m_compressedSize = static_cast<uint32_t>(compressedSize);
    std::swap(m_blob, compressedBlob);
    m_blobIsCompressed = true;

    return true;
}
