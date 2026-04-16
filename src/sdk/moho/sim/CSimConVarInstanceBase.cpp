#include "CSimConVarInstanceBase.h"

#include <cstdlib>

#include "moho/console/CConCommand.h"

using namespace moho;

namespace
{
  [[nodiscard]] const std::string*
  GetArgToken(const moho::CSimConCommand::ParsedCommandArgs& args, const std::size_t index) noexcept
  {
    if (index >= args.size()) {
      return nullptr;
    }
    return &args[index];
  }

  [[nodiscard]] float ParseFloatArg(const std::string* const token) noexcept
  {
    if (token == nullptr) {
      return 0.0f;
    }
    return static_cast<float>(std::atof(token->c_str()));
  }

  [[nodiscard]] bool TokenEq(const std::string* const token, const char* const text) noexcept
  {
    return token != nullptr && text != nullptr && *token == text;
  }

  [[nodiscard]] bool TokenEqNoCase(const std::string* const token, const char* const text) noexcept
  {
    return token != nullptr && text != nullptr && gpg::STR_EqualsNoCase(token->c_str(), text);
  }

  [[nodiscard]] int ParseIntArg(const std::string* const token) noexcept
  {
    return std::atoi(token != nullptr ? token->c_str() : "");
  }

  template <typename TValue>
  void ApplyIntegralSimConVarArgs(const moho::CSimConCommand::ParsedCommandArgs& args, TValue* const value) noexcept
  {
    if (value == nullptr) {
      return;
    }

    const std::string* const op = GetArgToken(args, 1u);
    const std::string* const rhs = GetArgToken(args, 2u);

    if (TokenEq(op, "=") && rhs != nullptr) {
      *value = static_cast<TValue>(ParseIntArg(rhs));
      return;
    }
    if (TokenEq(op, "+=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value + static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "-=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value - static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "*=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value * static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "/=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value / static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "%=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value % static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "&=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value & static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "|=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value | static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }
    if (TokenEq(op, "^=") && rhs != nullptr) {
      *value = static_cast<TValue>(*value ^ static_cast<TValue>(ParseIntArg(rhs)));
      return;
    }

    if (TokenEqNoCase(op, "on") || TokenEqNoCase(op, "true")) {
      *value = static_cast<TValue>(1);
      return;
    }
    if (TokenEqNoCase(op, "off") || TokenEqNoCase(op, "false")) {
      *value = static_cast<TValue>(0);
      return;
    }
    if (TokenEqNoCase(op, "tog")) {
      *value = static_cast<TValue>((*value == static_cast<TValue>(0)) ? 1 : 0);
      return;
    }

    if (op != nullptr) {
      *value = static_cast<TValue>(ParseIntArg(op));
    }
  }

  void ApplyIntSimConVarArgs(const moho::CSimConCommand::ParsedCommandArgs& args, int* const value) noexcept
  {
    ApplyIntegralSimConVarArgs<int>(args, value);
  }

  void ApplyUInt8SimConVarArgs(const moho::CSimConCommand::ParsedCommandArgs& args, std::uint8_t* const value) noexcept
  {
    ApplyIntegralSimConVarArgs<std::uint8_t>(args, value);
  }

  void ApplyFloatSimConVarArgs(const moho::CSimConCommand::ParsedCommandArgs& args, float* const value) noexcept
  {
    if (value == nullptr) {
      return;
    }

    const std::string* const op = GetArgToken(args, 1u);
    const std::string* const rhs = GetArgToken(args, 2u);

    if (op == nullptr) {
      return;
    }

    if (*op == "=" && rhs != nullptr) {
      *value = ParseFloatArg(rhs);
      return;
    }
    if (*op == "+=" && rhs != nullptr) {
      *value += ParseFloatArg(rhs);
      return;
    }
    if (*op == "-=" && rhs != nullptr) {
      *value -= ParseFloatArg(rhs);
      return;
    }
    if (*op == "*=" && rhs != nullptr) {
      *value *= ParseFloatArg(rhs);
      return;
    }
    if (*op == "/=" && rhs != nullptr) {
      *value /= ParseFloatArg(rhs);
      return;
    }

    *value = ParseFloatArg(op);
  }
} // namespace

/**
 * Address: 0x00579740 (FUN_00579740, sub_579740)
 *
 * What it does:
 * Base convar-instance destructor body; deleting behavior is provided by
 * compiler-generated scalar-deleting thunk in the original binary.
 */
CSimConVarInstanceBase::~CSimConVarInstanceBase() = default;

/**
 * Address: 0x0057FCC0 (FUN_0057FCC0, Moho::TSimConVarInstance_int::OnCall)
 *
 * What it does:
 * Handles int sim-convar command arguments and prints current value when no
 * RHS token is provided.
 */
template <>
int moho::TSimConVarInstance<int>::HandleConsoleCommand(void* commandArgs)
{
  auto* const args = static_cast<CSimConCommand::ParsedCommandArgs*>(commandArgs);
  if (args != nullptr && args->size() >= 2u) {
    ApplyIntSimConVarArgs(*args, &mValue);
    return 1;
  }

  CON_Printf("int %s == %d", mName ? mName : "", mValue);
  return 0;
}

/**
 * Address: 0x00735990 (FUN_00735990, Moho::TSimConVarInstance_uint8::OnCall)
 *
 * What it does:
 * Handles uint8 sim-convar command arguments and prints current value when no
 * RHS token is provided.
 */
template <>
int moho::TSimConVarInstance<std::uint8_t>::HandleConsoleCommand(void* commandArgs)
{
  auto* const args = static_cast<CSimConCommand::ParsedCommandArgs*>(commandArgs);
  if (args != nullptr && args->size() >= 2u) {
    ApplyUInt8SimConVarArgs(*args, &mValue);
    return 1;
  }

  CON_Printf("uint8 %s == %d", mName ? mName : "", static_cast<int>(mValue));
  return 0;
}

/**
 * Address: 0x005D41E0 (FUN_005D41E0, Moho::TSimConVarInstance_float::GetPtr)
 *
 * What it does:
 * Returns pointer to the float value lane stored at offset +0x08.
 */
template <>
void* moho::TSimConVarInstance<float>::GetValueStorage()
{
  return &mValue;
}

/**
 * Address: 0x0057FC80 (FUN_0057FC80, Moho::TSimConVarInstance_bool::GetRRef)
 *
 * What it does:
 * Builds one reflected bool reference for this convar value and copies it
 * into `outRef`.
 */
template <>
gpg::RRef* moho::TSimConVarInstance<bool>::GetValueRef(gpg::RRef* const outRef)
{
  if (outRef == nullptr) {
    return nullptr;
  }

  gpg::RRef boolValueRef{};
  gpg::RRef_bool(&boolValueRef, &mValue);
  outRef->mObj = boolValueRef.mObj;
  outRef->mType = boolValueRef.mType;
  return outRef;
}

/**
 * Address: 0x005D41F0 (FUN_005D41F0, Moho::TSimConVarInstance_float::GetRRef)
 *
 * What it does:
 * Builds one reflected float reference for this convar value and copies it
 * into `outRef`.
 */
template <>
gpg::RRef* moho::TSimConVarInstance<float>::GetValueRef(gpg::RRef* const outRef)
{
  if (outRef == nullptr) {
    return nullptr;
  }

  gpg::RRef floatValueRef{};
  gpg::RRef_float(&floatValueRef, &mValue);
  outRef->mObj = floatValueRef.mObj;
  outRef->mType = floatValueRef.mType;
  return outRef;
}

/**
 * Address: 0x005D4180 (FUN_005D4180, Moho::TSimConVarInstance_float::OnCall)
 *
 * What it does:
 * Handles float sim-convar command arguments and prints current value when no
 * RHS token is provided.
 */
template <>
int moho::TSimConVarInstance<float>::HandleConsoleCommand(void* commandArgs)
{
  auto* const args = static_cast<CSimConCommand::ParsedCommandArgs*>(commandArgs);
  if (args != nullptr && args->size() >= 2u) {
    ApplyFloatSimConVarArgs(*args, &mValue);
    return 1;
  }

  CON_Printf("float %s == %.4f", mName ? mName : "", mValue);
  return 0;
}
