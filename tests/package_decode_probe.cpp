// Private corpus probe. Never shipped in the authoring kit.
#include "core/property_decoder.h"
#include <fstream>
#include <iostream>
static bool ReadBytes(const std::string& Path, std::vector<std::uint8_t>& Bytes)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    if (!Input) return false;
    const auto Size = Input.tellg();
    if (Size < 0 || Size > 64 * 1024 * 1024) return false;
    Bytes.resize(std::size_t(Size)); Input.seekg(0);
    if (!Bytes.empty()) Input.read(reinterpret_cast<char*>(Bytes.data()), std::streamsize(Bytes.size()));
    return bool(Input);
}
int main()
{
    using namespace P4MapKit;
    std::string Line;
    while (std::getline(std::cin, Line))
    {
        std::istringstream Input(Line); std::string HeaderPath, BodyPath, CodecText;
        std::getline(Input, HeaderPath, '\t'); std::getline(Input, BodyPath, '\t');
        std::vector<FMapStructCodec> Codecs; bool ValidCodecs = true;
        while (std::getline(Input, CodecText, '\t'))
        {
            std::istringstream Fields(CodecText); std::vector<std::string> Parts; std::string Part;
            while (std::getline(Fields, Part, '|')) Parts.push_back(Part);
            if (Parts.size() != 6 || (Parts[4] != "0" && Parts[4] != "1") || (Parts[5] != "0" && Parts[5] != "1"))
            { ValidCodecs = false; break; }
            Codecs.push_back({Parts[0], Parts[1], Parts[2], Parts[3], Parts[4] == "1", Parts[5] == "1"});
        }
        std::vector<std::uint8_t> Header, Body;
        FDecodedValue Result, Document;
        FReadError Error{0, "unreadable-probe-input"};
        if (ValidCodecs && ReadBytes(HeaderPath, Header) && ReadBytes(BodyPath, Body))
        {
            const auto Inventory = ReadPackage(Header, Body);
            FPropertyDecoder Decoder(Inventory, Body, 1000000, 64 * 1024 * 1024, Codecs);
            Decoder.Document(Document); Error = Decoder.Error;
            if (Error.Code.empty())
            {
                FPropertyDecoder TypedDecoder(Inventory, Body, 1000000, 64 * 1024 * 1024, Codecs);
                FDecodedValue Typed;
                if (!TypedDecoder.TypedDocument(Typed)) Error = TypedDecoder.Error;
                else if (DecodedJson(Document) != DecodedJson(Typed) ||
                    DecodedJson(Document) != DecodedJson(LegacyDecodedValue(Typed)))
                    Error = {0, "typed-legacy-projection-mismatch"};
            }
        }
        Result.Fields.emplace_back("valid", FDecodedValue::Atom(FDecodedValue::EKind::Boolean, Error.Code.empty() ? "true" : "false"));
        Result.Fields.emplace_back("error", FDecodedValue::Atom(FDecodedValue::EKind::String, Error.Code));
        Result.Fields.emplace_back("offset", FDecodedValue::Atom(FDecodedValue::EKind::Number, std::to_string(Error.Offset)));
        if (Error.Code.empty()) Result.Fields.emplace_back("document", std::move(Document));
        WriteDecodedJson(Result, std::cout); std::cout << '\n';
    }
    return 0;
}
