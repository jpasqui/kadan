// Preserve absent versus explicitly serialized optional postprocess settings.
// An absent property is not an original ARM64 constructor/default certificate.
#pragma once
#include "material_cooked.h"
#include "material_json.h"
#include "material_lifter.h"
namespace P4MapKit
{
namespace Material
{
inline Json RetainOriginalPostProcessSerializedOptions(const FMaterialCookedRecord& master, Json graph)
{
    if (master.Class != "Material" || master.Properties.Kind != FDecodedValue::EKind::Object ||
        graph.At("domain").Text() != "MD_PostProcess" || graph.At("render_settings").kind != Json::Object)
        throw Refused("postprocess serialized options require the actual typed Material root",
                      "material source post settings");
    auto& settings = graph["render_settings"];
    for (const auto* name : {"BlendableOutputAlpha", "bIsBlendable", "BlendablePriority"})
    {
        const auto field = master.Properties.Field(name);
        if (!field)
        {
            settings.object.erase(name);
            continue;
        }
        if (!settings.Has(name))
            throw Refused("explicit postprocess setting is missing from recovered graph",
                          "material source post settings");
        bool sameValue = false;
        if (std::string(name) == "BlendablePriority")
        {
            std::int64_t value = 0;
            std::istringstream input(field->Text);
            input >> value;
            if (field->Kind == FDecodedValue::EKind::Number && !input.fail() && input.eof() &&
                value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max() &&
                settings.At(name).kind == Json::Number)
                sameValue = settings.At(name).number == double(value);
        }
        else
        {
            bool value = false;
            sameValue = MaterialBool(*field, value) && settings.At(name).kind == Json::Boolean &&
                        settings.At(name).boolean == value;
        }
        if (!sameValue)
            throw Refused("explicit postprocess setting differs from its actual serialized value",
                          "material source post settings");
    }
    return graph;
}
}
} // P4MapKit::Material
