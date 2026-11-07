#include "../core/material_execution_resources.h"
#include <iostream>

namespace M = P4MapKit::Material;
int main()
{
    try
    {
        M::MaterialOriginalImportCustody Source;
        Source.Asset.Package.Package = "/Game/Controls/Master";
        Source.Asset.Package.Imports = {
            {"/Script/CoreUObject","Package","/Game/Collections/MPC_Control",0},
            {"/Script/Engine","MaterialParameterCollection","MPC_Control",-1},
            {"/Script/Engine","MaterialParameterCollection","MPC_Control",-1},
            {"/Script/Engine","Texture2D","CachedTexture",-1},
            {"/Script/Engine","MaterialFunction","UnusedFunction",-1}
        };
        std::vector<M::FMaterialExecutionResource> Edges;
        M::MaterialExecutionResourceDetail::Collections(Source,Edges);
        if(Edges.size()!=1||Edges[0].Kind!=M::EMaterialExecutionResourceKind::ParameterCollection||
            Edges[0].NativeClass!="/Script/Engine.MaterialParameterCollection"||Edges[0].ImportIndex!=-2||
            Edges[0].Package!="/Game/Collections/MPC_Control"||Edges[0].Object!="/Game/Collections/MPC_Control.MPC_Control"||
            Edges[0].Property!="Imports[1]")throw std::runtime_error("typed collection prerequisite or duplicate exclusion differs");
        // Familiar names without the actual native class do not authorize a collection edge.
        Source.Asset.Package.Imports[1].Class = "Texture2D";
        Source.Asset.Package.Imports[2].Class = "Texture2D";
        Edges.clear();M::MaterialExecutionResourceDetail::Collections(Source,Edges);
        if(!Edges.empty())throw std::runtime_error("a name or untyped dependency became a collection");
        Source.Asset.Package.Imports[1].Class = "MaterialParameterCollection";
        Source.Asset.Package.Imports[1].Outer = -100;
        bool Refused = false;
        try { M::MaterialExecutionResourceDetail::Collections(Source,Edges); }
        catch(const M::Refused&) { Refused = true; }
        if(!Refused)throw std::runtime_error("an invalid source import was accepted");
        Source.Asset.Package.Imports[1].Outer = -1;
        Source.Asset.Package.Imports[0].Name = "/Engine/Collections/MPC_Control";
        Refused = false;
        try { M::MaterialExecutionResourceDetail::Collections(Source,Edges); }
        catch(const M::Refused&) { Refused = true; }
        if(!Refused)throw std::runtime_error("a non-game object became a game prerequisite");
        std::cout << "PASS: typed collection prerequisites, duplicate exclusion, unrelated imports and invalid identities\n";
        return 0;
    }
    catch(const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
}
