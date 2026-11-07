#include "P4RecoveredParticleSystem.h"
#include "P4MapKitCore/particle_cutout.h"
#include "Particles/ParticleModuleRequired.h"
#include "Engine/Texture2D.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/RenderingObjectVersion.h"

namespace P4OriginalParticleCutout
{
bool Decode(const TArray<uint8>& Tail, P4MapKit::ParticleCutout::FRequiredTail& Decoded, FString& Error)
{
    std::string Why;
    if (!P4MapKit::ParticleCutout::Read(Tail.GetData(), std::size_t(Tail.Num()), Decoded, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    return true;
}
bool Matches(const TArray<FVector2D>& Geometry, const P4MapKit::ParticleCutout::FRequiredTail& Decoded)
{
    static_assert(sizeof(FVector2D) == 8, "Source cutouts require two IEEE binary32 coordinates");
    return std::size_t(Geometry.Num()) * 2 == Decoded.CoordinateBits.size() &&
        (Geometry.Num() == 0 ||
            FMemory::Memcmp(Geometry.GetData(), Decoded.CoordinateBits.data(), Geometry.Num() * 8) == 0);
}
bool Regenerated(
    const UParticleModuleRequired* Module, const P4MapKit::ParticleCutout::FRequiredTail& Decoded, FString& Error)
{
    if (!Module || Module->GetClass() != UParticleModuleRequired::StaticClass())
    {
        Error = TEXT("particle-required-native-class-unqualified");
        return false;
    }
    if (Module->SubImages_Horizontal < 1 || Module->SubImages_Vertical < 1 ||
        int64(Module->SubImages_Horizontal) * Module->SubImages_Vertical > 131072 ||
        (Module->BoundingMode != BVC_FourVertices && Module->BoundingMode != BVC_EightVertices) ||
        !FMath::IsFinite(Module->AlphaThreshold) || Module->OpacitySourceMode < OSM_Alpha ||
        Module->OpacitySourceMode > OSM_BlueChannel)
    {
        Error = TEXT("particle-required-cutout-parameters-unqualified");
        return false;
    }
    if (!Module->CutoutTexture)
    {
        if (Decoded.CoordinateBits.empty())
            return true;
        Error = TEXT("particle-required-cutout-texture-missing");
        return false;
    }
#if WITH_EDITORONLY_DATA
    if (!Module->CutoutTexture->Source.IsValid())
    {
        Error = TEXT("particle-required-cutout-texture-source-missing");
        return false;
    }
    auto& TextureSource = Module->CutoutTexture->Source;
    if (TextureSource.GetFormat() != TSF_BGRA8 || TextureSource.GetSizeX() < Module->SubImages_Horizontal ||
        TextureSource.GetSizeY() < Module->SubImages_Vertical ||
        int64(TextureSource.GetSizeX()) * TextureSource.GetSizeY() > MAX_int32 / 4)
    {
        Error = TEXT("particle-required-cutout-source-pixel-layout-unqualified");
        return false;
    }
    TArray64<uint8> Mip;
    if (!TextureSource.GetMipData(Mip, 0) ||
        Mip.Num() != int64(TextureSource.GetSizeX()) * TextureSource.GetSizeY() * 4)
    {
        Error = TEXT("particle-required-cutout-source-pixels-unreadable");
        return false;
    }
    const std::size_t Expected = std::size_t(Module->GetNumFrames()) * Module->GetNumBoundingVertices() * 2;
    if (Decoded.CoordinateBits.size() != Expected)
    {
        Error = TEXT("particle-required-cutout-frame-count");
        return false;
    }
    FSubUVDerivedData Scratch;
    Scratch.Build(Module->CutoutTexture, Module->SubImages_Horizontal, Module->SubImages_Vertical, Module->BoundingMode,
        Module->AlphaThreshold, Module->OpacitySourceMode);
    if (!Matches(Scratch.BoundingGeometry, Decoded))
    {
        Error = TEXT("particle-required-original-geometry-not-regenerated-from-source-pixels");
        return false;
    }
    return true;
#else
    Error = TEXT("particle-required-source-pixel-proof-needs-editor");
    return false;
#endif
}
} //namespace P4OriginalParticleCutout

bool P4DescribeOriginalParticleRequiredTail(const TArray<uint8>& Tail, TArray<FString>& NativeLines, FString& Error)
{
    P4MapKit::ParticleCutout::FRequiredTail Decoded;
    if (!P4OriginalParticleCutout::Decode(Tail, Decoded, Error))
        return false;
    TArray<FString> Candidate;
    Candidate.Add(
        FString::Printf(TEXT("particle required cooked cutout vertices %d"), int32(Decoded.CoordinateBits.size() / 2)));
    for (std::size_t Index = 0; Index < Decoded.CoordinateBits.size(); Index += 2)
        Candidate.Add(FString::Printf(TEXT("particle required cutout vertex %d words %08x %08x"), int32(Index / 2),
            Decoded.CoordinateBits[Index], Decoded.CoordinateBits[Index + 1]));
    NativeLines = MoveTemp(Candidate);
    Error.Reset();
    return true;
}

bool P4OriginalParticleRequiredTailMatches(
    const UParticleModuleRequired* Module, const TArray<uint8>& Tail, FString& Error)
{
    P4MapKit::ParticleCutout::FRequiredTail Decoded;
    if (!P4OriginalParticleCutout::Decode(Tail, Decoded, Error))
        return false;
    if (!Module || Module->GetClass() != UParticleModuleRequired::StaticClass())
    {
        Error = TEXT("particle-required-native-class-unqualified");
        return false;
    }
    // CreateRendererResource returns a value copy of the real private array, including its length.
    // Do not index GetFrameData until that length has been independently established.
    TUniquePtr<FParticleRequiredModule> Resource(
        const_cast<UParticleModuleRequired*>(Module)->CreateRendererResource());
    if (!P4OriginalParticleCutout::Matches(Resource->FrameData, Decoded))
    {
        int32 First = INDEX_NONE;
        uint32 ExpectedWord = 0, ActualWord = 0;
        const std::size_t Words = FMath::Min(std::size_t(Resource->FrameData.Num()) * 2, Decoded.CoordinateBits.size());
        for (std::size_t Index = 0; Index < Words; ++Index)
        {
            uint32 Word = 0;
            FMemory::Memcpy(&Word, reinterpret_cast<const uint8*>(Resource->FrameData.GetData()) + Index * 4, 4);
            if (Word != Decoded.CoordinateBits[Index])
            {
                First = int32(Index);
                ExpectedWord = Decoded.CoordinateBits[Index];
                ActualWord = Word;
                break;
            }
        }
        Error = FString::Printf(
            TEXT(
                "particle-required-native-geometry-differs: %s expected vertices %d actual %d first word %d source %08x native %08x"),
            *Module->GetPathName(), int32(Decoded.CoordinateBits.size() / 2), Resource->FrameData.Num(), First,
            ExpectedWord, ActualWord);
        return false;
    }
    Error.Reset();
    return true;
}

bool P4ParticleRequiredTailRegeneratesFromTextureSource(
    const UParticleModuleRequired* Module, const TArray<uint8>& Tail, FString& Error)
{
    P4MapKit::ParticleCutout::FRequiredTail Decoded;
    if (!P4OriginalParticleCutout::Decode(Tail, Decoded, Error))
        return false;
    if (!P4OriginalParticleCutout::Regenerated(Module, Decoded, Error))
        return false;
    Error.Reset();
    return true;
}

bool P4ApplyOriginalParticleRequiredTail(UParticleModuleRequired* Module, const TArray<uint8>& Tail, FString& Error)
{
    P4MapKit::ParticleCutout::FRequiredTail Decoded;
    if (!P4OriginalParticleCutout::Decode(Tail, Decoded, Error) ||
        !P4OriginalParticleCutout::Regenerated(Module, Decoded, Error))
        return false;
    // An empty tagged-property list leaves every already-applied source property intact.
    // The retained tail supplies the false GUID marker, cooked flag, and exact FVector2D array.
    TArray<uint8> Envelope;
    FMemoryWriter Memory(Envelope, true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    FName None = NAME_None;
    static_cast<FArchive&>(Writer) << None;
    Envelope.Append(Tail);
    FMemoryReader Input(Envelope, true);
    FObjectAndNameAsStringProxyArchive Reader(Input, false);
    Reader.SetCustomVersion(
        FRenderingObjectVersion::GUID, FRenderingObjectVersion::LatestVersion, TEXT("RenderingObjectVersion"));
    FStructuredArchiveFromArchive Structured(Reader);
    // Dispatch the installed engine's real virtual serializer; no MinimalAPI symbol substitution.
    static_cast<UObject*>(Module)->Serialize(Structured.GetSlot().EnterRecord());
    if (Reader.IsError() || Input.Tell() != Envelope.Num())
    {
        Error = TEXT("particle-required-native-envelope-not-consumed");
        return false;
    }
    return P4OriginalParticleRequiredTailMatches(Module, Tail, Error);
}
