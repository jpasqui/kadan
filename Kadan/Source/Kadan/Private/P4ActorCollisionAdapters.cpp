#include "P4ActorCollision.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

FString P4ActorCollisionJson(const FP4ActorCollisionResult& R)
{
    FString Error;
    switch (R.Error)
    {
        case EP4ActorCollisionError::None:
            break;
        case EP4ActorCollisionError::EditorThreadRequired:
            Error = TEXT("navigation-collision-requires-editor-thread");
            break;
        case EP4ActorCollisionError::ActorRequired:
            Error = TEXT("navigation-collision-needs-an-actor");
            break;
        case EP4ActorCollisionError::FileRequired:
            Error = TEXT("navigation-collision-needs-a-file");
            break;
        case EP4ActorCollisionError::AbsoluteFileRequired:
            Error = TEXT("navigation-collision-needs-a-whole-path");
            break;
        case EP4ActorCollisionError::GeometryUnreadable:
            Error = TEXT("navigation-collision-unreadable: ") + R.Component;
            break;
        case EP4ActorCollisionError::NoCollision:
            Error = TEXT("navigation-collision-the-actor-has-none");
            break;
        case EP4ActorCollisionError::FileWriteFailed:
            Error = TEXT("navigation-collision-file-not-written");
            break;
    }
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), R.IsValid());
    Json->SetStringField(TEXT("error"), Error);
    if (R.GeometryRead)
    {
        Json->SetNumberField(TEXT("components"), R.Components);
        Json->SetNumberField(TEXT("vertices"), R.Vertices);
        Json->SetNumberField(TEXT("triangles"), R.Triangles);
        Json->SetStringField(TEXT("file"), R.File);
    }
    FString Text = P4CondensedJson(Json);
    return Text;
}
