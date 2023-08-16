
#include <assert.h>
#include <sqconfig.h>

#include "sqcompilationcontext.h"
#include "sqstring.h"

#include "keyValueFile.h"

namespace SQCompilation {

struct DiagnosticDescriptor {
  const char *format;
  enum DiagnosticSeverity severity;
  const int32_t id;
  const char *textId;
  bool disabled;
};

static const char severityPrefixes[] = "hwe";
static const char *severityNames[] = {
  "Hint", "Warning", "Error", nullptr
};

static DiagnosticDescriptor diagnsoticDescriptors[] = {
#define DEF_DIAGNOSTIC(_, severity, ___, num_id, text_id, fmt) { _SC(fmt), DS_##severity, num_id, _SC(text_id), false }
  DIAGNOSTICS
#undef DEF_DIAGNOSTIC
};

SQCompilationContext::SQCompilationContext(SQVM *vm, Arena *arena, const SQChar *sn, const char *code, size_t csize, bool raiseError)
  : _vm(vm)
  , _arena(arena)
  , _sourceName(sn)
  , _linemap(_ss(vm)->_alloc_ctx)
  , _code(code)
  , _codeSize(csize)
  , _raiseError(raiseError)
{
    if (code) {
        buildLineMap();
    }
}

SQCompilationContext::~SQCompilationContext()
{
}

std::vector<std::string> SQCompilationContext::function_forbidden;
std::vector<std::string> SQCompilationContext::function_can_return_string;
std::vector<std::string> SQCompilationContext::function_should_return_bool_prefix;
std::vector<std::string> SQCompilationContext::function_should_return_something_prefix;
std::vector<std::string> SQCompilationContext::function_result_must_be_utilized;
std::vector<std::string> SQCompilationContext::function_can_return_null;
std::vector<std::string> SQCompilationContext::function_calls_lambda_inplace;
std::vector<std::string> SQCompilationContext::function_forbidden_parent_dir;
std::vector<std::string> SQCompilationContext::function_modifies_object;
std::vector<std::string> SQCompilationContext::function_must_be_called_from_root;

std::vector<std::string> SQCompilationContext::std_identifier;
std::vector<std::string> SQCompilationContext::std_function;

void SQCompilationContext::resetConfig() {

  function_forbidden = {

  };

  function_can_return_string = {
    "subst",
    "concat",
    "tostring",
    "toupper",
    "tolower",
    "slice",
    "trim",
    "join",
    "format",
    "replace"
  };

  function_should_return_bool_prefix = {
    "has",
    "Has",
    "have",
    "Have",
    "should",
    "Should",
    "need",
    "Need",
    "is",
    "Is",
    "was",
    "Was",
    "will",
    "Will"
  };

  function_should_return_something_prefix = {
    "get",
    "Get"
  };

  function_result_must_be_utilized = {
    "__merge",
    "indexof",
    "findindex",
    "findvalue",
    "len",
    "reduce",
    "tostring",
    "tointeger",
    "tofloat",
    "slice",
    "tolower",
    "toupper"
  };

  function_can_return_null = {
    "indexof",
    "findindex",
    "findvalue"
  };

  function_calls_lambda_inplace = {
    "findvalue",
    "findindex",
    "__update",
    "filter",
    "map",
    "reduce",
    "each",
    "sort",
    "assert",
    "persist",
    "join",
  };

  function_forbidden_parent_dir = {
    "require",
    "require_optional",
  };

  function_modifies_object = {
    "extend",
    "append",
    "__update",
    "insert",
    "apply",
    "clear",
    "sort",
    "reverse",
    "resize",
    "rawdelete",
    "rawset",
  };

  function_must_be_called_from_root = {
    "keepref"
  };

  std_identifier = {
    "require",
    "require_optional",
    "vargv",
    "persist",
    "getclass",
    "__name__",
    "__filename__",
    "keepref",
  };

  std_function = {
    "seterrorhandler",
    "setdebughook",
    "getstackinfos",
    "getroottable",
    "getconsttable",
    "getclass",
    "assert",
    "print",
    "error",
    "compilestring",
    "newthread",
    "suspend",
    "array",
    "type",
    "callee",
    "collectgarbage",
    "resurrectunreachable",
    "min",
    "max",
    "clamp",
  };

}
bool SQCompilationContext::loadConfigFile(const char *configFile) {
  KeyValueFile config;
  if (!config.loadFromFile(configFile)) {
    return false;
  }

  //for (auto && v : config.getValuesList("format_function_name"))
  //{
  //  string functionName(v);
  //  std::transform(functionName.begin(), functionName.end(), functionName.begin(), ::tolower);
  //  format_function_name.push_back(functionName);
  //}

  for (auto && v : config.getValuesList("forbidden_function"))
    function_forbidden.push_back(v);

  for (auto && v : config.getValuesList("function_can_return_null"))
    function_can_return_null.push_back(v);

  for (auto && v : config.getValuesList("function_calls_lambda_inplace"))
    function_calls_lambda_inplace.push_back(v);

  for (auto && v : config.getValuesList("std_identifier"))
    std_identifier.push_back(v);

  for (auto && v : config.getValuesList("std_function"))
    std_function.push_back(v);

  for (auto && v : config.getValuesList("function_result_must_be_utilized"))
    function_result_must_be_utilized.push_back(v);

  for (auto && v : config.getValuesList("function_can_return_string"))
    function_can_return_string.push_back(v);

  for (auto && v : config.getValuesList("function_should_return_bool_prefix"))
    function_should_return_bool_prefix.push_back(v);

  for (auto && v : config.getValuesList("function_should_return_something_prefix"))
    function_should_return_something_prefix.push_back(v);

  for (auto && v : config.getValuesList("function_forbidden_parent_dir"))
    function_forbidden_parent_dir.push_back(v);

  for (auto && v : config.getValuesList("function_modifies_object"))
    function_modifies_object.push_back(v);

  for (auto && v : config.getValuesList("function_must_be_called_from_root"))
    function_must_be_called_from_root.push_back(v);

  return true;
}

void SQCompilationContext::buildLineMap() {
  assert(_code != NULL);

  int prev = '\n';

  for (size_t i = 0; i < _codeSize; ++i) {
    if (prev == '\n') {
      _linemap.push_back(&_code[i]);
    }

    prev = _code[i];
  }
}

const char *SQCompilationContext::findLine(int lineNo) {
  lineNo -= 1;
  if (lineNo < 0 || lineNo >= _linemap.size())
    return nullptr;

  return _linemap[lineNo];
}

static const char *strstr_nl(const char *str, const char *fnd) {

  int len = strlen(fnd);

  while (*str != '\0' && *str != '\n') {
    if (*str == *fnd) {
      if (strncmp(str, fnd, len) == 0) {
        return str;
      }
    }
    ++str;
  }

  return nullptr;
}

void SQCompilationContext::printAllWarnings(FILE *ostream) {
  for (auto &diag : diagnsoticDescriptors) {
    if (diag.severity == DS_ERROR)
      continue;
    fprintf(ostream, "w%d (%s)\n", diag.id, diag.textId);
    fprintf(ostream, diag.format, "***", "***", "***", "***", "***", "***", "***", "***");
    fprintf(ostream, "\n\n");
  }
}

void SQCompilationContext::flipWarningsState() {
  for (auto &diag : diagnsoticDescriptors) {
    if (diag.severity == DS_ERROR)
      continue;
    diag.disabled = !diag.disabled;
  }
}

bool SQCompilationContext::switchDiagnosticState(const char *diagName, bool state) {
  for (auto &diag : diagnsoticDescriptors) {
    if (strcmp(diagName, diag.textId) == 0) {
      if (diag.severity != DS_ERROR) {
        diag.disabled = !state;
      }
      return true;
    }
  }
  return false;
}

bool SQCompilationContext::switchDiagnosticState(int32_t id, bool state) {
  for (auto &diag : diagnsoticDescriptors) {
    if (id == diag.id) {
      if (diag.severity != DS_ERROR) {
        diag.disabled = !state;
      }
      return true;
    }
  }
  return false;
}

bool SQCompilationContext::isDisabled(enum DiagnosticsId id, int line, int pos) {
  DiagnosticDescriptor &descriptor = diagnsoticDescriptors[id];
  if (descriptor.severity >= DS_ERROR) return false;

  const char *codeLine = findLine(line);

  if (!codeLine)
    return false;

  char suppressLineIntBuf[64] = { 0 };
  char suppressLineTextBuf[128] = { 0 };
  snprintf(suppressLineIntBuf, sizeof(suppressLineIntBuf), "//-%c%d", severityPrefixes[descriptor.severity], descriptor.id);
  int lt = snprintf(suppressLineTextBuf, sizeof(suppressLineTextBuf), "//-%s", descriptor.textId);

  if (strstr_nl(codeLine, suppressLineIntBuf) || strstr_nl(codeLine, suppressLineTextBuf)) {
    return true;
  }

  char suppressFileIntBuf[64] = { 0 };
  char suppressFileTextBuf[128] = { 0 };
  int fi = snprintf(suppressFileIntBuf, sizeof(suppressFileIntBuf), "//-file:%c%d", severityPrefixes[descriptor.severity], descriptor.id);
  int ft = snprintf(suppressFileTextBuf, sizeof(suppressFileTextBuf), "//-file:%s", descriptor.textId);

  if (strstr(_code, suppressFileIntBuf) || strstr(_code, suppressFileTextBuf)) {
    descriptor.disabled = true;
    return true;
  }

  return false;
}

static void drawUnderliner(int32_t column, int32_t width, std::string &msg)
{
  int32_t i = 0;
  while (i < column) {
    msg.push_back(' ');
    ++i;
  }

  ++i;
  msg.push_back('^');

  while ((i - column) < width) {
    msg.push_back('-');
    ++i;
  }
}


static bool isBlankLine(const char *l) {
  if (!l) return true;
  while (*l && *l != '\n') {
    if (!isspace(*l)) return false;
    ++l;
  }
  return true;
}

void SQCompilationContext::vreportDiagnostic(enum DiagnosticsId diagId, int32_t line, int32_t pos, int32_t width, va_list vargs) {
  assert(diagId < DI_NUM_OF_DIAGNOSTICS);

  if (isDisabled(diagId, line, pos)) {
    return;
  }

  auto &desc = diagnsoticDescriptors[diagId];
  bool isError = desc.severity >= DS_ERROR;
  char tempBuffer[2048] = { 0 };
  std::string message;

  int32_t i = snprintf(tempBuffer, sizeof tempBuffer, "%s: ", severityNames[desc.severity]);

  message.append(tempBuffer);

  int len = vsnprintf(tempBuffer, sizeof tempBuffer, desc.format, vargs);

  message.append(tempBuffer);

  const char *l1 = findLine(line - 1);
  const char *l2 = findLine(line);
  const char *l3 = findLine(line + 1);

  if (!isBlankLine(l1)) {
    message.push_back('\n');
    int32_t j = 0;
    while (l1[j] && l1[j] != '\n') {
      message.push_back(l1[j++]);
    }
  }

  if (!isBlankLine(l2)) {
    message.push_back('\n');
    int32_t j = 0;
    while (l2[j] && l2[j] != '\n') {
      message.push_back(l2[j++]);
    }

    message.push_back('\n');
    j = 0;

    drawUnderliner(pos, width, message);
  }

  if (!isBlankLine(l3)) {
    message.push_back('\n');
    int32_t j = 0;
    while (l3[j] && l3[j] != '\n') {
      message.push_back(l3[j++]);
    }
  }

  if (l1 || l2 || l3) {
    message.push_back('\n');
  }

  auto errorFunc = _ss(_vm)->_compilererrorhandler;

  const char *msg = message.c_str();

  if (_raiseError && errorFunc) {
    errorFunc(_vm, msg, _sourceName, line, pos);
  }
  if (isError) {
    _vm->_lasterror = SQString::Create(_ss(_vm), msg, message.length());
    longjmp(_errorjmp, 1);
  }
}

void SQCompilationContext::reportDiagnostic(enum DiagnosticsId diagId, int32_t line, int32_t pos, int32_t width, ...) {
  va_list vargs;
  va_start(vargs, width);
  vreportDiagnostic(diagId, line, pos, width, vargs);
  va_end(vargs);
}

} // namespace SQCompilation
