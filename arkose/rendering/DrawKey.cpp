#include "DrawKey.h"

#include "asset/MaterialAsset.h"

DrawKey::DrawKey(Brdf brdf, BlendMode blendMode, bool doubleSided)
    : m_brdf(brdf)
    , m_blendMode(blendMode)
    , m_doubleSided(doubleSided)
{
}

DrawKey DrawKey::generate(MaterialAsset* materialAsset)
{
    ARKOSE_ASSERT(materialAsset != nullptr);
    return DrawKey(Brdf::GgxMicrofacet, materialAsset->blendMode, materialAsset->doubleSided);
}

u32 DrawKey::asUint32() const
{
    u32 drawKeyU32 = 0;

    // 4-bits for BRDF (i.e., up to 16 BRDFs)
    u32 brdfBits = static_cast<u32>(m_brdf);
    ARKOSE_ASSERT(brdfBits < 0x0f);
    drawKeyU32 = drawKeyU32 | brdfBits;

    // 3-bits for blend mode (i.e., up to 8 blend modes)
    u32 blendModeBits = static_cast<u32>(m_blendMode);
    ARKOSE_ASSERT(blendModeBits < 0x08);
    drawKeyU32 = (drawKeyU32 << 3) | blendModeBits;

    // 1-bit for double sided
    u32 doubleSidedBit = static_cast<u32>(m_doubleSided);
    drawKeyU32 = (drawKeyU32 << 1) | doubleSidedBit;

    // Set MSB to 1 so that we never have a valid draw key u32 of value 0,
    // since we want to be able to use the draw key as a mask.
    drawKeyU32 |= (1 << 31);

    return drawKeyU32;
}
