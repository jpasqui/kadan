#pragma once
// UE4's MSVC default is /fp:fast. Authoring readers must preserve IEEE values
// and reject nonfinite input even in ordinary UBT builds of this module.
#if defined(_MSC_VER)
#pragma float_control(precise, on)
#endif
