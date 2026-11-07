// One step of original-source recovery and how an ordered table of steps runs. A table holds, in order, the steps
// that prepare what its producers read, the producers, and the steps that complete their result: every admitted
// preparation step runs, then the first admitted producer, then every admitted completion step.
#pragma once
#include <vector>
namespace P4MapKit
{
namespace Material
{
enum class MaterialSourceStage
{
    Prepare,
    Produce,
    Complete
};
template <class Dispatch>
struct MaterialSourceProducer
{
    const char* Name;
    MaterialSourceStage Stage;
    bool (*Admits)(const Dispatch&);
    void (*Run)(Dispatch&);
};
template <class Dispatch>
inline void RunMaterialSourceProducers(const std::vector<MaterialSourceProducer<Dispatch>>& table, Dispatch& dispatch)
{
    bool produced = false;
    for (const auto& producer : table)
    {
        if (producer.Stage == MaterialSourceStage::Produce && produced)
            continue;
        if (!producer.Admits(dispatch))
            continue;
        producer.Run(dispatch);
        if (producer.Stage == MaterialSourceStage::Produce)
            produced = true;
    }
}
}
} // P4MapKit::Material
