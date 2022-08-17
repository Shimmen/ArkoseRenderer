#pragma once

#include <memory>
#include <vector>
#include <string_view>

class Image;

// Binary blob type for Arkose
class Arkblob {
public:

    Arkblob() = default;
    virtual ~Arkblob() { }

    enum class Type : uint32_t {
        Empty = 0,
        Image,
        Material,
        Mesh,
        Scene,
        __Count
    };

    static std::unique_ptr<Arkblob> makeImageBlob(Image const&);
    static std::unique_ptr<Image> readImageFromBlob(std::string_view filePath);

    static const char* fileExtensionForType(Type);
    static Type typeFromFilename(std::string_view);

    bool readFromFile(std::string_view);
    bool writeToFile(std::string_view) const;

    bool isEmpty() const;
    Type type() const { return m_type; }

    bool isImage() const { return type() == Type::Image; }
    std::unique_ptr<Image> decodeImage();

private:

    void writeHeader(std::ostream&) const;
    bool readHeader(std::istream&);

    bool compressBlob();

    void encodeImage(Image const&);

    static constexpr const char* HeaderMagic = "arkblob";

    Type m_type { Type::Empty };

    uint32_t m_compressedSize { 0 };
    uint32_t m_uncompressedSize { 0 };

    // Is m_blob compressed or not right now?
    bool m_blobIsCompressed { false };

    // Actual blob data to (optionally) compress and write to file
    std::vector<uint8_t> m_blob {};

};
