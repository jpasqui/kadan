// Certificates of the folded lighting family: what was reviewed in the game's files when its contracts were first
// written, which production code no longer holds. Every folded lighting owner of the game is found by its data; for
// each, every vertex program paired with its pixel programs must be one of the reviewed producers (payload and
// instruction digests, factory, policy, named buffers), and the Bubble owner's distortion programs must be the five
// sealed ones. A change to the game's files fails here, not in recovery.
//
//     material_folded_certificates_test <shader archive> <extracted game root> <material index json>
#include "../core/material_masters.h"
#include "../core/native_custom_backend_archive.h"
#include <fstream>
#include <iostream>
namespace M = P4MapKit::Material;
namespace C = P4MapKit::CustomMaterial;

static C::Bytes File(const std::string& path, std::uint64_t bound)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("file unavailable: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || std::uint64_t(size) > bound)
        throw std::runtime_error("file outside its bound: " + path);
    input.seekg(0);
    C::Bytes bytes(std::size_t(size), 0);
    if (size && !input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("file unreadable: " + path);
    return bytes;
}

struct MaterialFoldedReviewedVertex
{
    const char* Factory;
    std::vector<std::string> Types;
    std::vector<std::string> Buffers;
    const char* Code;
    bool Lightmap, SH;
};
// material_folded_engine_varyings.REVIEWED: original vertex payload SHA-256 -> reviewed producer.
static const std::map<std::string, MaterialFoldedReviewedVertex>& ReviewedVertices()
{
    static const std::map<std::string, MaterialFoldedReviewedVertex> reviewed = {
        {"051349f4feb9ee0fb9adf65ddfb85606518244f43704605c4a7e0d410fdce119",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "4e1309cbd76f8e6d0963a5a4a3ce92d53cb2968abfb271179da94b830fbf59e5",
          false,
          true}},
        {"1370b9b97531845f48d6ccfba51fedee9b2bd7f6127d54ae439b3b73a4f4771a",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "4ae045d6cd280e52385b1eb787407d2745de6ff85c693fa85d4fb3f9fc175148",
          false,
          false}},
        {"13e94dbc85e58911b055a7d14a2c0918e485398143f2d724e270e05baccae4d4",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\"}"},
          "3bc169b05fb7ad9f5cdbc5b46160b25e2e1cc8e8d0fefb8a61f841ebb7295167",
          false,
          true}},
        {"1a548ee32897e2e3ffec75bd59a40b7a445f9bcd59bf3200363131d4046dee8c",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "e12d04c7a7af756b6bbca74539a0cd565dab8b197e1736971f94fc9d7cc33eaa",
          false,
          true}},
        {"1bcab2d5b2f132dd39a705f09f6521290f6ef1c6a82f4bbb64acb87129cfa516",
         {"FMeshParticleVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"Material\"}"},
          "7c0ca50a6621480f89b315fa325ec705d5a1108aef008dc27c4df2a30966e71d",
          false,
          true}},
        {"1dceb4098ce89d2c2db8b147a270f298fce1783eed70949911c41a3c6bbcc029",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\"}"},
          "21c1234d0e00a7e50c50833ccabf76f72fece313e2996bd0046c54bfb89a0e8a",
          false,
          false}},
        {"1e004f10bc3182aef9b795dc259a094b8504c5b17bdea462b85e46b792bb22d7",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\",\"4\":\"Material\"}"},
          "1f3dc79918cdcb9c95d3bf6a5908dde278efff14978490342649bf55624634ec",
          false,
          false}},
        {"208282cc2aca573bde284d131d2e08c4cd522d84af2a5f5859f1137842aaf68d",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "292cc9b6c97d015276af8a1a7d996017332624478b269f19f0442d0c7c766e74",
          false,
          true}},
        {"20b7d8c64b5d96efa87a924894e6dac20138d77715788832a452f8d6f4f5c1d3",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "60deac21345878fd3d2041a7e78531f7573c781b1270bc76ab2dcbb366b0ac76",
          false,
          false}},
        {"21cba4ea050e048d7736b01e385e1e3df819307ff5a56d51c4166cdac0809e83",
         {"FLocalVertexFactory",
          {"TBasePassVSTLightMapPolicyHQ"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"PrecomputedLightingBuffer\",\"5\":\"Material\"}"},
          "6dc5599a9a1664c2f4841d5a798c2a970d03fb4a0e188a8b8b31ca0eddb40bb1",
          true,
          false}},
        {"2315d07720874cdd97c0beffcf01a7d999d3e408764ddb53b92cb9426d490d91",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "90799623f1c54595e9c2d776c5f534abd0468987b01015faff1bfbd26fec62c4",
          false,
          false}},
        {"2cebb2518706bbc3e24415cde90b9f1f8b6743dcccef4f1d8b930492217a1339",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\",\"4\":\"Material\"}"},
          "d75a0e212764ad29543912c7c876123ad1175f11e75b6cb994fc00fe1b51701f",
          false,
          false}},
        {"2df934fa389811ecef5f1ea4268b27bb8fc1c078ca1f3f051e7c795130ceeec7",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\"}"},
          "15005d5225e09145e74138f20c88f29ec2e24642c1cc3f68dfe5d96e0a4cae62",
          false,
          false}},
        {"2ec390b58d79e0fa889d6311bd982cc22ee4112aec82c4a8210418c74c642a96",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\",\"4\":\"Material\"}"},
          "f8c72cabd80af5dd006a66a1fc9c361c26c0c0f6c1192fd0cecd375e7ec7f266",
          false,
          true}},
        {"3560519431ef26cec49e5c4d8185689cd517a32882ac7396046b9fc678b3707c",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\"}"},
          "49ccb3f2740f95ef332c1e8c214962eb597d7d1553823423b1f46c51d08af325",
          false,
          false}},
        {"36f79f10b75e1c8844ce6f169c7a4845a60ae7617322657045d206962d54f9c1",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "07c182b1b348c7d5cea1d88b399ee90cfda8bfc56dbd55fcef3eedc1fc654db8",
          false,
          true}},
        {"4f4bb17b341b82447113a208ccc747ab704004b9ddece63095dd4b22673d6c20",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\"}"},
          "0e45948777ac58a269905fd2e3e99aa8b0e3fceeb397804bd6fda85b3ea3fbe8",
          false,
          true}},
        {"4fb3ee88ae033bbba5f3b64a85b756fbe91ee86111b46358a254d6b0c2b9ed45",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "3b417ba5b2b5fc446891192e99023829844cf4f28e9f627e70104e1fd57c833e",
          false,
          true}},
        {"520ceb063ce4c8a4012b562fa2b7d4a594580fcf2cd4e597bf0c3ae4665ebb2e",
         {"FMeshParticleVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"Material\"}"},
          "1220cbbc93173784378acbcbcce3cef8bff861acb39e4873ef672c8adaf1eed9",
          false,
          false}},
        {"580855987a6807d80dbc0028e30afec3f68f9555936de972022d5501979b5f5b",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\"}"},
          "e0a7f94ebcba653bd9f09edb90021c63599477398e02f25bb2c8095c4b2d8fbf",
          false,
          true}},
        {"5c48f22a81ba64e0388627e447b13893e66025f94a723a4adb88d85efbce8610",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "149de432b05d6a8f01cfba64b3032670d710e9a25cb46e2aa915490402308541",
          false,
          true}},
        {"5f0ad19191ddde77d9c9783117573f3c06f701e48f027fd7c322a4c177b06b25",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\",\"4\":\"Material\"}"},
          "16802bc58df0a9e3b433c05dfa1b0aa842c988f9298255858f70db95d74f87c9",
          false,
          true}},
        {"5f6c1bc3eab7e3fee66e5457e65e81118088ef05741d18b6fbf0744b4222c35a",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "dd8132ec95ed63676f896ed767065f3acc0ba5a0e7a4e38909eea6b90787868d",
          false,
          true}},
        {"635a964711aef840ac07e605a04ace62384379c81e909a8d14ed0e4fd9d7f3c0",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "63e0e880f26ddfa16d810155699aa8c04c5aa1f85620e06eccd56f504d0ac4e0",
          false,
          false}},
        {"63ac6e7b6e5f76da469e53ba55fcde1111dbff76bc5e344ea71ad027846bfdc6",
         {"FLocalVertexFactory",
          {"TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"PrecomputedLightingBuffer\"}"},
          "a324a607cb90648ad3e951c9c58087b567ac8007bea6858922728d9a34deabbe",
          true,
          false}},
        {"63e17bfdde599b98e03b7010ba20c1c06361705bf96325d816b0a91a4262b114",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\"}"},
          "2b2b733cc290166d6a7af788c004ef763d0c2c214a966cd4200100b33d57472f",
          false,
          true}},
        {"6490dda09c99d107afa3871ac3160c777dd987da15ad3a4ac44bc9642773bcf5",
         {"FLocalVertexFactory",
          {"TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"PrecomputedLightingBuffer\",\"5\":\"Material\"}"},
          "10532f26437d69ca8ff98e1ca1d5eb2a8329afa2a37735a4e68c3684020c1502",
          true,
          false}},
        {"66e876ccf29c8a844bdcc409ec08a31ebace0ddfd367c1ea86f72c89c5a038d1",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\"}"},
          "74ff3370fbdbe8878c7e56d416cd460dc5835b7d929f254c2b4b0f55a1a19b04",
          false,
          true}},
        {"6ba3ac0d80ca644ebd66cdeeffa198c69c576e683650e41a295df0c8d5737fc2",
         {"FMeshParticleVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"Material\"}"},
          "9923365dfd0ca502b35a6be8e0d5a89353f09a75c33895a51bc9ab454a626fe9",
          false,
          false}},
        {"6bc0df038b390066b98ddb03e3691d392b0e804a0a14728c8f5520117cd63767",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\"}"},
          "9e80d0cf98332ee2f90e9c54b5cb8d97e9aed6994eaf89c524039387f5a7277a",
          false,
          false}},
        {"6d9a16983b8e407006e9d9827e7af4cacc13828e45793b031869ff9fccb279cb",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\"}"},
          "26b2db5f048ed69ff544d8c9b5a75cd64bbf7e3e0c640cd9b911584fc0ef7ed0",
          false,
          true}},
        {"6e222e9c8e4e6d69dc10aae890a9b4a06ffae4754585a503c9cd4e1e6a8bc5fa",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "1e4d7d39dee99f8bc44b2b33f63556c0604776142bb2cb8f48d8c44486dfa1a9",
          false,
          false}},
        {"6ef211e7b540927c2ee7d6591769ee62b17e0515bdf54d15882f7341f5e7c58b",
         {"FMeshParticleVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"Material\"}"},
          "49bded924a49e7fba047cce8cc3ad3a649a95da46886969a27ece703ce75aa23",
          false,
          true}},
        {"7281688331e46c9e981480ea714c253a5c2fce205bacf4b95d1180862e9419a0",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "e9cbf5ba6d401ee1dba2eec9c4345b23181087aaa489fb6100490c45ce6504cf",
          false,
          true}},
        {"7939adebf37a0fd7a712882bcd84757e76b5d68091edebc72e21bd869a685a06",
         {"TGPUSkinVertexFactoryDefault",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\"}"},
          "9bdd607c17d1f87f55edccd63634743d75a30ef51359878d4b615073160b914f",
          false,
          false}},
        {"7e9eea0dda90512277f2069a7c7871dc6aa47a214674f00ba115680f4e185ecc",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "091e70d904e13edf3d804cb58bf042bcf655402568ecd48f1881caa9fc6060aa",
          false,
          false}},
        {"816a564f3e19a5a0ae4c583c0ac94158ba2d64be3171be4194c626adde678d3b",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "d5ad92df1fd914e6fc0699acceda574b4b4e81815ff153d8ebe28df36d3e5b8d",
          false,
          false}},
        {"86343bb5c61c81a908b3ad294796a99d601486a166a1399e920b96a1584fae25",
         {"TGPUSkinVertexFactoryDefault",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\"}"},
          "0676025589e673837d3f70df2a47706c4ef0a7bd69307958528bba56b7f2d57e",
          false,
          true}},
        {"87a607d9ede887dc28d83fd3339206adf0f3ce6865ea333ba4b6268aebe0972a",
         {"FMeshParticleVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\"}"},
          "59248f939c22e21d298495f2f92487dc3952d19c6a9fa718b892bc50412cb22d",
          false,
          true}},
        {"896599856182c0dbdff70e9424cb090464b3f51295ec8bd8bd2fd3daa7433430",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "e1b799088571a1ac3a0a9ce6a742b7835dc465fdc724c97111c7230afb91edff",
          false,
          true}},
        {"9017d835039f84f616b3ac06edf35a6b9e2c6c81e50e3d287b4e88172b5e0f60",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\"}"},
          "982af644af6d865f3329241b74c489738ac5eb1998575a873311f0ed5cbf2703",
          false,
          false}},
        {"932f665fd2967d8ae2074015abfff2701e9d093d6b89fa8278a53eeb5dc06ed4",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\",\"4\":\"Material\"}"},
          "6ced240d3023610c2f2dca91350ab85afcd585ef92df369d31c67712c1567acd",
          false,
          false}},
        {"99be5d17e4a424bf66e938040f17d84e20887d94f16a833ce566d4534bb0f014",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "0ade2732d84730211fc082809d1ea0454ec50cb763dd0fd7bdb2ecf397196fe1",
          false,
          false}},
        {"9bbe182f26138c8106a478571c7574e1725301297fd9e32d42fa606578fe96d8",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "5ba9ef7790ef1652f911efb2aad3352354253fa1b8dcb5b4b17c5044d7084f89",
          false,
          true}},
        {"9d5b207be0e3df84e9478eb6bbdb74f4041c0bb99641eeb7c46da7e980c27241",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "645a0ac03e94211dc547deba4435019eea2d4b72c72957065d0583bf1e9043fa",
          false,
          false}},
        {"9dfaca4e2333ab11081d6c6ed0c35f568b28dc52868de61239b20ca641b002e1",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "5deec79791b78e9619c52e787c49a5b96e3953e7dfa8eb94b1a7c030f0bb4eb7",
          false,
          false}},
        {"a2aa85ed4c746220b659552a966149738dac9f34271036a68ddc0b3f8711d3f3",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "b9eef336de0d553e721e0d3687f353660079d1253885671d1a8a3d549b7e9ecd",
          false,
          false}},
        {"a2e7ad097baa69773d6b109f0f662510f2032f971be1420ffc3485498a19ddd0",
         {"FLocalVertexFactory",
          {"TBasePassVSTLightMapPolicyHQ"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"PrecomputedLightingBuffer\",\"5\":\"Material\"}"},
          "e8aa4a55d6f10886082d236bba71fa4cea8dc4a545ecc266bc27a699786833cb",
          true,
          false}},
        {"a4830f92299979665f41745d87f91c690537e317d91c393f7852bf3c56f351d9",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "9d1edab1db6cea20bcfbb9f8bda89aae645a9340241a408481346b253ede6fe6",
          false,
          false}},
        {"a841b5378346220e296682795e481889d208c3afdae17b5ecf84979185f08468",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\"}"},
          "0aa4123e2fccf758f549e2b9bd2b96cb01f6c06b52fb9c9faac2078a5bce8130",
          false,
          true}},
        {"abd594537d007b9cb90d77126a9b69ee11868770a7c855569777888080cb9680",
         {"FMeshParticleVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\"}"},
          "ca4b5fa7e28e73a8f119086514676378baa1b471af237b2c59cbeede88d8732d",
          false,
          false}},
        {"adb29dab9f0e9170ede788d1f69b4eb974176b479683dd1ef695c0c2d2ee450f",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\"}"},
          "5bcc0e67177d0c70217ff76bdcf12eb089dd75835457531f66500c701fd3afa1",
          false,
          false}},
        {"af8a297037e16e89ff2744b1b9e888684a9b07c5f96832cc59d7856aed11e9cc",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\"}"},
          "f31d8440c9914ba9c35ada73e2fe934782b368b06f7a950b997809f5f572575f",
          false,
          false}},
        {"b672ec6b1ab589b9fb73824d72eb9c85fe1e86e2447c6adad6e0f0690e9d648e",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "e496e26a7f95dd118439d1a2cd360e0a75f89f1fd0fadb533415d06e4fb6188c",
          false,
          true}},
        {"b7d77df3e9e9a07099f2bcfcd15b6ea5ff07f16927639f586ce5ebeabb029763",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\"}"},
          "6f8af8e8b4c776fe7ab6eeff40c8a0b315c245bca7a3f25eb0f938d54bd97d17",
          false,
          false}},
        {"c01dfa9e1b0cae816fbca43186480e2ee653e86422cf2b38a17dfb5a64069da9",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "d48559183daf7fadbbc5630dda3835f22bc4ac5ebb4660d49a852e7c9ead1b5d",
          false,
          true}},
        {"c7e3691321c90c26e223474aa4bebd3bb99ed8ab7800650d83d6e0b84e1bc4d3",
         {"FLocalVertexFactory",
          {"TBasePassVSTLightMapPolicyHQ"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"PrecomputedLightingBuffer\"}"},
          "f41d908c87d46c36d964bec3f488732430a679b9c86be89f80b52265337557ea",
          true,
          false}},
        {"cb38840d1e5c0cfb0f291eb2482e8757f2639ad12d2501b6d53065e80754b2ed",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "f05bb1cf5835dcea9b7626bdd84524eb0d4939d279994c8aeb6cd9f30a46cfdc",
          false,
          true}},
        {"d42befde6c9dc65612127032b8a4999cd411ffe5a8cd18f936477910e54036aa",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\"}"},
          "27550e4e09d7ecf14f1ea2792df05fb73cb0780ee6b085edb4b7eba78172ef65",
          false,
          false}},
        {"d5056a014b30c8bb2aa5a9bac9d6b2740dc1ceb3e95bd8fdba2f376f21dc804d",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\",\"5\":\"Material\"}"},
          "35ae3d0261a64d6b16a8857b6101c959b852e7c2042f7825950549130c558d34",
          false,
          false}},
        {"d76c929b89d7e73d40e5f1ed08f543c5abdd5335808865a98d131a08b00bad29",
         {"FGPUSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"64c64e3dd42f6064\",\"4\":\"3e44a39f6365f2b3\"}"},
          "4c84327bf6506853297f46119277e97331c2d0446052d2415af2b500fa85de30",
          false,
          true}},
        {"e00cf744f55401cfe46efb43d20ee26804121f99923eeb38b2f46b2bb243e42a",
         {"FParticleSpriteVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"8eb448214a2d2367\",\"4\":\"Material\"}"},
          "b475074f6daf22f6788bc6266d8894bcdd3416f0bfc8a5a1a2783cf956115fea",
          false,
          true}},
        {"e8e4abd178661c8ca3140d55d985d0f726b483498254b6b6ecf091d9ca1e19b4",
         {"FLocalVertexFactory",
          {"TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"PrecomputedLightingBuffer\",\"5\":\"Material\"}"},
          "79b25ecb11bc60fb71eac88b4d18af57fe8a44c677ee8a1ceb4c16b66f84920e",
          true,
          false}},
        {"ee996cc052b5a7af6cecade2d7565d4ca6ebb413a85290382f38e4c38fdff81f",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "b159c72f1135ad6f0b322d0e3c617938fa866d08832c79699af667050fcc25b2",
          false,
          false}},
        {"f04e861b86e29b352dfc96ce4ac668166c162897b70d82e54e07d898e504f85f",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\"}"},
          "215119fa5c9d706c73e09d63e843e030724fb42ed3bbb45046a89e5650793b40",
          false,
          true}},
        {"f2b4a6161d94e28008279f1781140f6c0fad4e51c563b7a23d0a2ebf291456ce",
         {"FLocalVertexFactory",
          {"TBasePassVSFNoLightMapPolicy", "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy",
           "TBasePassVSFSelfShadowedTranslucencyPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "6ae750dc6c5573b8ceb76c2162bc1ed569279ff7a7fcd3343b55505bed0e99ee",
          false,
          false}},
        {"fd038d7ce460bc6b651dacbd7d5c4cdb15a876f4364f7eb034a39e5f9994d7fa",
         {"FParticleBeamTrailVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"83c95d3294e72e70\",\"4\":\"Material\"}"},
          "864be3beca40b65761fc032e0a75b202c2f82723841c0dcdabe3f8b1a91eed73",
          false,
          true}},
        {"fe43c26ba736e04ba060e6947c1b03c06d81ebc55331337e743773771f5fe168",
         {"FLocalVertexFactory",
          {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
           "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
          {"{\"1\":\"View\",\"2\":\"Primitive\",\"3\":\"LocalVF\",\"4\":\"Material\"}"},
          "cb0d5fa490dcefe32c2161bbcbefdafecf22f68f3922c72db6acd03985574e7e",
          false,
          true}}};
    return reviewed;
}

// The reviewed producer of one paired vertex program, checked as material_folded_engine_varyings.prepare did.
static void CheckReviewedVertex(const M::MaterialFoldedVertexPairing& pairing, const std::string& factory,
                                const M::MaterialOriginalArchiveAccess& archive, std::set<std::string>& seen)
{
    const auto payload = archive.Payload(pairing.VertexIndex);
    const auto digest = M::PayloadDigest(payload);
    const auto record = ReviewedVertices().find(digest);
    if (record == ReviewedVertices().end() || factory != record->second.Factory ||
        std::find(record->second.Types.begin(), record->second.Types.end(), pairing.VertexType) ==
            record->second.Types.end())
        throw std::runtime_error("vertex payload/factory/policy is not a reviewed producer: " + digest);
    const auto header = M::ReadOriginalParticleVertexBindings(payload);
    const auto lines = M::MaterialInstructionLines(M::ReadOriginalMaterialProgramText(payload, header));
    if (M::ProgramDigest(lines) != record->second.Code)
        throw std::runtime_error("vertex producer instruction digest differs: " + digest);
    if (std::find(record->second.Buffers.begin(), record->second.Buffers.end(), pairing.Buffers) ==
        record->second.Buffers.end())
        throw std::runtime_error("vertex producer named buffers differ: " + digest);
    seen.insert(digest);
}

// material_bubble_distortion_contract.recover_input's seal of the Bubble owner's distortion programs.
static void CheckBubbleDistortionSeal(const M::MaterialOriginalOwnerSource& owner,
                                      const M::MaterialOriginalArchiveAccess& archive)
{
    static const std::map<std::uint32_t, const char*> payloads = {
        {3739, "6e2d22a0c777dd9e6ee1edb6687e3f15dcefd90f6b4061f3ec0f11f0615acc84"},
        {2643, "dddc6e2244302aafa4782bb5abdfe3fe17a101055a209335b5bb9511c94f8345"},
        {1119, "504342aa33be0826a30b0ab98b18ae74006d2d093a003ef13b8c596eadb68d8a"},
        {4598, "72a8bdb5b9818f0ce3b56c923e44810bc2d2d61522ea51c4655b678f8546da7f"}};
    static const std::map<std::string, std::uint32_t> factories = {{"FGPUSpriteVertexFactory", 3739},
                                                                   {"FParticleSpriteVertexFactory", 2643},
                                                                   {"TGPUSkinVertexFactoryDefault", 1119},
                                                                   {"FMeshParticleVertexFactory", 4598},
                                                                   {"FLocalVertexFactory", 1119}};
    const std::uint64_t distortion = 0x8b7ebf7dbd1d1758ull;
    std::vector<P4MapKit::FMaterialShaderReference> refs;
    P4MapKit::FReadError error;
    if (!P4MapKit::ReadMaterialShaderReferences(owner.Map, refs, error))
        throw M::Refused(error.Code, "Bubble distortion contract");
    std::vector<P4MapKit::FMaterialShaderReference> stage;
    std::set<std::string> seen;
    for (const auto& ref : refs)
        if (ref.Frequency == 3 && ref.TypeHash == distortion)
        {
            stage.push_back(ref);
            seen.insert(M::MaterialFactoryName(ref.VertexFactoryHash));
        }
    std::set<std::string> expected;
    for (const auto& item : factories)
        expected.insert(item.first);
    if (seen != expected || stage.size() != 5)
        throw M::Refused("Bubble auxiliary five-factory inventory changed", "Bubble distortion contract");
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    for (const auto& ref : stage)
    {
        const auto factory = M::MaterialFactoryName(ref.VertexFactoryHash);
        const auto index = M::ArchiveIndexOf(indices, ref, "Bubble auxiliary shader/factory identity changed",
                                             "Bubble distortion contract");
        const auto known = payloads.find(index);
        if (known == payloads.end() || factories.at(factory) != index)
            throw M::Refused("Bubble auxiliary shader/factory identity changed", "Bubble distortion contract");
        const auto payload = archive.Payload(index);
        if (M::PayloadDigest(payload) != known->second)
            throw M::Refused("Bubble auxiliary native payload changed", "Bubble distortion contract");
        P4MapKit::CustomMaterial::ShaderBindings header;
        std::string failure;
        try
        {
            header = P4MapKit::CustomMaterial::ReadPixelBindings(payload);
        }
        catch (const std::exception& e)
        {
            failure = e.what();
        }
        if (!failure.empty())
            throw M::Refused("Bubble auxiliary native payload changed", "Bubble distortion contract");
        if (header.layout_hashes !=
                std::vector<std::uint32_t>{P4MapKit::EngineHash::ViewLayout, 0x098F0609u, 72157431u} ||
            M::BubbleSourceDetail::TableEntries(header.textures, 1) != std::vector<std::pair<int, int>>{{1, 0}} ||
            M::BubbleSourceDetail::TableEntries(header.samplers, 1) != std::vector<std::pair<int, int>>{{13, 0}})
            throw M::Refused("Bubble auxiliary scene-depth resource/layout/sampler changed",
                             "Bubble distortion contract");
        if (M::OriginalFoldedStableBuffers(ref) != "{\"0\":\"View\",\"2\":\"Material\"}" ||
            !M::BubbleSourceDetail::TableEntries(header.textures, 2).empty())
            throw M::Refused("Bubble auxiliary typed buffer/material texture bindings changed",
                             "Bubble distortion contract");
        const auto names = M::MaterialParticleVaryingNames(header.inputs);
        const std::map<std::string, int> varying = factory == "FGPUSpriteVertexFactory"
                                                       ? std::map<std::string, int>{{"in_tangentz", 3}}
                                                       : std::map<std::string, int>{{"in_texcoord11", 1}};
        if (names != varying)
            throw M::Refused("Bubble auxiliary vertex normal input changed", "Bubble distortion contract");
    }
    // _preshader: the shared Material uniform programs place the two direct parameters at words 28/29.
    const auto& uniforms = owner.Map.Uniforms;
    if (uniforms.VectorPrograms.size() != 9 || uniforms.ScalarPrograms.size() != 30)
        throw M::Refused("Bubble auxiliary Material uniform programs changed", "Bubble distortion contract");
    const auto direct = [](const P4MapKit::FMaterialPreshaderProgram& program, std::uint16_t parameter)
    {
        return program.Instructions.size() == 1 &&
               program.Instructions[0].Opcode == P4MapKit::EMaterialPreshaderOpcode::ScalarParameter &&
               program.Instructions[0].Parameter == parameter;
    };
    if (!direct(uniforms.ScalarPrograms[28], 16) || !direct(uniforms.ScalarPrograms[29], 17))
        throw M::Refused("Bubble auxiliary Material uniform programs changed", "Bubble distortion contract");
    const auto& scalars = uniforms.Scalars;
    if (scalars.size() != 18 || scalars[16].Name != "XluCutWaterHeight" || scalars[17].Name != "RefractionDepthBias" ||
        M::BubbleSourceDetail::ScalarDefault(scalars[16]) != 0. ||
        M::BubbleSourceDetail::ScalarDefault(scalars[17]) != 0.)
        throw M::Refused("Bubble auxiliary direct parameter identity changed", "Bubble distortion contract");
    for (const auto& pair : {std::make_pair(-.7, 3.1), std::make_pair(0., 0.), std::make_pair(1.2, -2.3)})
    {
        std::vector<double> values;
        for (std::size_t i = 0; i < scalars.size(); ++i)
            values.push_back(17. + double(i));
        values[16] = pair.first;
        values[17] = pair.second;
        std::vector<std::array<double, 4>> vectors(uniforms.Vectors.size(), std::array<double, 4>{{0, 0, 0, 0}});
        const auto algebra = P4MapKit::MaterialNumericPreshaderAlgebra(values, vectors);
        for (const auto& check :
             {std::make_pair(std::size_t(28), pair.first), std::make_pair(std::size_t(29), pair.second)})
        {
            std::array<double, 4> out;
            P4MapKit::FReadError failure;
            if (!P4MapKit::EvaluateMaterialPreshader(uniforms.ScalarPrograms[check.first], algebra, out, failure) ||
                out[0] != check.second)
                throw M::Refused("Bubble auxiliary parameter programs do not implement the named inputs",
                                 "Bubble distortion contract");
        }
    }
    if (const auto mode = owner.User.Chain.front().Properties.Field("RefractionMode"))
    {
        std::string value;
        if (!P4MapKit::MaterialEnum(*mode, value) || (value != "RM_IndexOfRefraction" && value != "0"))
            throw M::Refused("Bubble distortion requires RM_IndexOfRefraction, not PixelNormalOffset",
                             "Bubble distortion contract");
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("shader archive, extracted game root and material index required");
        const auto bytes = File(argv[1], 1ull << 31);
        C::NativeBackend::ShaderArchive shaders(bytes);
        const std::string root = argv[2];
        const auto indexBytes = File(argv[3], 1ull << 28);
        const auto index = M::Json::Parse(std::string(indexBytes.begin(), indexBytes.end()));
        M::MaterialOriginalArchiveAccess archive;
        archive.MapIndices = [&](const std::array<std::uint8_t, 20>& hash)
        {
            return shaders.Map(C::Bytes(hash.begin(), hash.end()));
        };
        archive.Payload = [&](std::uint32_t at)
        {
            return shaders.Payload(at);
        };
        archive.Frequency = [&](std::uint32_t at)
        {
            return std::uint8_t(shaders.Frequency(at));
        };
        const std::function<void(M::SymbolicArena&, M::BindingContext&)> binding;
        std::set<std::string> seen;
        int owners = 0, pairings = 0, bubble = 0;
        for (const auto& item : index.object)
        {
            if (item.second.Has("error"))
                continue;
            M::MaterialMasterSources sources;
            sources.Raw = [&root](const std::string& package, M::MaterialOriginalImportRawAsset& out)
            {
                if (package.compare(0, 6, "/Game/") != 0)
                    return false;
                const auto stem = root + "/Carrot4/Content/" + package.substr(6);
                std::ifstream exists(stem + ".uasset", std::ios::binary);
                if (!exists)
                    return false;
                out = {std::make_shared<const C::Bytes>(File(stem + ".uasset", 64u * 1024u * 1024u)),
                       std::make_shared<const C::Bytes>(File(stem + ".uexp", 64u * 1024u * 1024u))};
                return true;
            };
            M::MaterialOriginalOwnerSource owner;
            try
            {
                M::MaterialSourceAsset asset;
                if (!sources.Resolver()(M::MaterialSourcePackage(item.first), asset))
                    continue;
                const auto chain = M::ReadOriginalMaterialParentChain(asset, sources.Resolver());
                bool own = chain.back().Class == "Material";
                if (!own)
                    if (const auto flag = chain.back().Properties.Field("bHasStaticPermutationResource"))
                        P4MapKit::MaterialBool(*flag, own);
                if (!own)
                    continue;
                owner = M::ReadOriginalMaterialOwnerSource(asset, sources.Resolver(), sources.Collections(asset));
            }
            catch (const std::exception&)
            {
                continue;
            }
            if (!M::OriginalFoldedLightingOwner(owner, archive, M::MaterialDomainSourceScope::ReadOnlyExpressions,
                                                binding))
                continue;
            ++owners;
            const auto factory =
                M::MaterialFactoryName(M::SelectOriginalMaterialPrimaryProgram(owner).VertexFactoryHash);
            for (const auto& policy : M::OriginalFoldedPolicyReferences(owner))
            {
                const auto varyings = M::PrepareOriginalFoldedEngineVaryings(owner, policy, archive);
                for (const auto& pairing : varyings.Pairings)
                {
                    CheckReviewedVertex(pairing, M::MaterialFactoryName(policy.VertexFactoryHash), archive, seen);
                    ++pairings;
                }
            }
            if (owner.Asset.Package.Package == "/Game/Carrot4/MasterMaterial/obj/M_obj_Bubble_Translucent_00_Master")
            {
                CheckBubbleDistortionSeal(owner, archive);
                ++bubble;
            }
        }
        if (bubble != 1)
            throw std::runtime_error("the Bubble owner's distortion seal was not checked");
        std::cout << owners << " folded lighting owners, " << pairings << " vertex pairings, " << seen.size() << " of "
                  << ReviewedVertices().size() << " reviewed producers seen, Bubble distortion seal held\n";
        return seen.empty() ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
