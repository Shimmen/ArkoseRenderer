#include "DrawKey.h"

#include "asset/MaterialAsset.h"

DrawKey::DrawKey(std::optional<Brdf> brdf, std::optional<BlendMode> blendMode, std::optional<bool> doubleSided, std::optional<bool> hasExplicitVelocity)
    : m_brdf(brdf)
    , m_blendMode(blendMode)
    , m_doubleSided(doubleSided)
    , m_hasExplicitVelocity(hasExplicitVelocity)
{
}

DrawKey DrawKey::generate(MaterialAsset* materialAsset)
{
    ARKOSE_ASSERT(materialAsset != nullptr);
    return DrawKey(materialAsset->brdf, materialAsset->blendMode, materialAsset->doubleSided, false);
}

u32 DrawKey::asUint32() const
{
    constexpr u32 brdfNumStates = 16;
    constexpr u32 blendModeNumStates = 8;
    constexpr u32 doubleSidedNumStates = 2;
    constexpr u32 explicitVelocityNumStates = 2;
    static_assert(brdfNumStates + blendModeNumStates + doubleSidedNumStates <= 32, "Needs to fit in 32-bits");

    u32 drawKeyU32 = 0;

    auto appendBits = [&]<typename EnumType>(std::optional<EnumType> maybeEnumValue, u32 numStates, bool first) {

        if (not first) {
            // Shift previous' bits so we have room for ours
            drawKeyU32 <<= numStates;
        }

        // If optional is empty, allow all combinations (i.e. set all bits to 1)
        u32 bits = (1 << numStates) - 1;

        if (maybeEnumValue.has_value()) {
            u32 stateIdx = static_cast<u32>(maybeEnumValue.value());
            ARKOSE_ASSERT(stateIdx < numStates);
            bits = 1 << stateIdx;
        }

        drawKeyU32 |= bits;
    };

    appendBits(m_brdf, brdfNumStates, true);
    appendBits(m_blendMode, blendModeNumStates, false);
    appendBits(m_doubleSided, doubleSidedNumStates, false);
    appendBits(m_hasExplicitVelocity, explicitVelocityNumStates, false);

    return drawKeyU32;
}

bool DrawKey::operator==(DrawKey const& other) const
{
    return m_brdf == other.m_brdf
        && m_blendMode == other.m_blendMode
        && m_doubleSided == other.m_doubleSided
        && m_hasExplicitVelocity == other.m_hasExplicitVelocity;
}

size_t DrawKey::calculateCompletePermutationSetCount()
{
    // TODO / OPTIMIZE: Calculate without generating the full list
    return createCompletePermutationSet().size();
}

std::vector<DrawKey> DrawKey::createCompletePermutationSet()
{
    std::vector<DrawKey> permutations;

    for (Brdf brdf : { Brdf::Default, Brdf::Skin }) {
        for (BlendMode blendMode : { BlendMode::Opaque, BlendMode::Masked, BlendMode::Translucent }) {
            for (bool doubleSided : { false, true }) {
                for (bool explicitVelocity : { false, true }) {
                    permutations.push_back(DrawKey(brdf, blendMode, doubleSided, explicitVelocity));
                }
            }
        }
    }

    return permutations;
}
