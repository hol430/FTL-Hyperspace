#include "Global.h"

HOOK_METHOD(TextInput, constructor,
    (int maxChars,
     TextInput::AllowedCharType allowedCharType,
     const std::string &prompt) -> void)
{
    LOG_HOOK("HOOK_METHOD -> TextInput::constructor -> Begin (TextInput.cpp)\n")

    super(maxChars, allowedCharType, prompt);
    lastPos = pos;
}
