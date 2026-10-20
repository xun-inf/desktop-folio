#!/usr/bin/env python3
"""folio schema + service proxy generator (merged from gen_protos.py / gen_services.py).

Generates two sets of checked-in artifacts from core/schemas:

1. C++ FlatBuffers protocol headers -> core/common/protos/<name>.h
   flatc 25 emits Get<Table>/Verify<Table>Buffer free functions only for the
   single root_type declared at the end of a schema. Each service schema has
   many Args tables that the media-service reads as roots, so we run flatc once
   per root table (with a temporary schema that declares only that root_type)
   and merge all generated root functions into one header:
     a. base header: schema without any root_type -> table definitions only;
     b. per-root header: schema with root_type X -> extract only the root
        function block (from the first "inline const services::" to the
        namespace closing brace);
     c. merged header: base header with every root block inserted right before
        the namespace closing brace.

2. node-media-service C++ same-name service proxies -> core/node-media-service/src/services/
   from core/schemas/*_service.fbs (NativeWindowService / StreamPreviewService):
     * class name = PascalCase(fbs stem without _service) + "Service", service_id = class name (both ends match);
     * methods = Args table encode + Transport::invoke + Result table decode, method name = camel(Args stem);
     * events = XxxEvent tables: event name = camel(without Event suffix); subscribeXxx() subscription with payload
       decode (handle: ulong fields -> DuplicateHandle Buffer), single listener, dispose() unregisters;
     * export rule = services/service_registry.{h,cpp} (ServiceFactory table + installAllServices() mounting each
       service proxy as a camel(serviceId) object on the root);
     * installXxxService() exposes each invoke method as an N-API function (returns a Promise).

Usage:
    python core/schemas/generate.py [--check]

Outputs (checked in; idempotently re-run at build time; C++ written as UTF-8 BOM + CRLF per AGENTS.md):
    core/common/protos/<name>.h
    core/common/protos/schema_traits.h        (root-table verify/get traits, ServiceBase dispatch)
    core/node-media-service/src/services/<name>_service.h/.cpp
    core/node-media-service/src/services/service_registry.h/.cpp   (unified ServiceFactory table + installAllServices)
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCHEMA_DIR = Path(__file__).resolve().parent
PROTOS_DIR = ROOT / "core" / "common" / "protos"
SERVICES_DIR = ROOT / "core" / "node-media-service" / "src" / "services"
FLATC = ROOT / "build" / "schema" / "_deps" / "folio_flatc-src" / "flatc.exe"

NAMESPACE_CLOSE = "\n}  // namespace services\n"

CPP_TYPES = {
    "string": ("const std::string&", "string"),
    "ulong": ("uint64_t", "ulong"),
    "long": ("int64_t", "long"),
    "uint": ("uint32_t", "uint"),
    "int": ("int32_t", "int"),
    "ushort": ("uint16_t", "ushort"),
    "short": ("int16_t", "short"),
    "byte": ("int8_t", "byte"),
    "ubyte": ("uint8_t", "ubyte"),
    "bool": ("bool", "bool"),
    "float": ("float", "float"),
    "double": ("double", "double"),
}

_FIELD_RE = re.compile(r"[ \t]*([A-Za-z0-9_]+)[ \t]*:[ \t]*([A-Za-z0-9_\[\]]+)[ \t]*;")
_TABLE_RE = re.compile(r"table[ \t]+([A-Za-z0-9_]+)[ \t]*{([^}]*)}", re.DOTALL)


def pascal(name):
    return "".join(part[:1].upper() + part[1:] for part in name.split("_") if part)


def camel(name):
    value = pascal(name)
    return value[:1].lower() + value[1:]


def snake(name):
    value = re.sub("([A-Z])", lambda m: "_" + m.group(1).lower(), name)
    return value.lstrip("_")


def parse_tables(text):
    tables = {}
    for match in _TABLE_RE.finditer(text):
        fields = []
        for field in _FIELD_RE.finditer(match.group(2)):
            fields.append((field.group(1), field.group(2)))
        tables[match.group(1)] = fields
    return tables


def collect_methods(tables):
    methods = []
    for name in sorted(tables):
        if not name.endswith("Args"):
            continue
        base = name[: -len("Args")]
        result = base + "Result"
        if result not in tables:
            continue
        methods.append((camel(base), base, tables[name], tables[result]))
    return methods




def collect_events(tables, roots):
    """XxxEvent tables = one event each: event name = camel(base without the Event suffix).
    The table must be a root_type (it travels as a FlatBuffer root on the wire)."""
    events = []
    for name in sorted(tables):
        if not name.endswith('Event'):
            continue
        base = name[:-len('Event')]
        if name not in roots:
            sys.exit('event table %s must be declared as root_type' % name)
        events.append((camel(base), base, tables[name]))
    return events
def cpp_type(field_type):
    if field_type.startswith("[") and field_type.endswith("]"):
        inner = field_type[1:-1]
        return "const std::vector<" + CPP_TYPES[inner][0] + ">"
    return CPP_TYPES[field_type][0]


def encode_exprs(builder_var, fields, indent):
    prelude = []
    args = []
    for name, field_type in fields:
        if field_type == "string":
            prelude.append(indent + "const auto " + name + "Offset = " + builder_var + ".CreateString(" + name + ");")
            args.append(name + "Offset")
        elif field_type.startswith("[") and field_type.endswith("]"):
            args.append(builder_var + ".CreateVector(" + name + ")")
        else:
            args.append(name)
    return prelude, args


def signature(params):
    if not params:
        return ""
    return ", ".join(ctype + " " + name for name, field_type, ctype in params)


def arg_read(field_type, index):
    """N-API argument read expression (per fbs field type)."""
    if field_type == "string":
        return "text(env, args[" + str(index) + "])"
    if field_type == "ulong":
        return "ulongValue(env, args[" + str(index) + "])"
    if field_type == "long":
        return "longValue(env, args[" + str(index) + "])"
    if field_type == "uint":
        return "uint32Value(env, args[" + str(index) + "])"
    if field_type == "int":
        return "int32Value(env, args[" + str(index) + "])"
    if field_type == "bool":
        return "boolValue(env, args[" + str(index) + "])"
    if field_type in ("float", "double"):
        return "numberValue(env, args[" + str(index) + "])"
    if field_type.startswith("[") and field_type.endswith("]"):
        raise ValueError("vector arguments are not supported by the N-API binding yet: " + field_type)
    return "uint32Value(env, args[" + str(index) + "])"


def property_value(name, field_type, value_expr):
    """Result table field -> JS value expression. value_expr looks like value->xxx()."""
    if field_type == "string":
        return "string(env, " + value_expr + " ? " + value_expr + "->str() : std::string())"
    if field_type == "bool":
        return "boolean(env, " + value_expr + ")"
    if field_type in ("ulong", "long", "uint", "int", "ushort", "short", "byte", "ubyte", "float", "double"):
        return "number(env, static_cast<double>(" + value_expr + "))"
    if field_type.startswith("[") and field_type.endswith("]"):
        inner = field_type[1:-1]
        if inner == "string":
            return "stringArray(env, " + value_expr + ")"
        return "numberArray(env, " + value_expr + ")"
    raise ValueError("unsupported result field type: " + field_type)


def render_decode(base, result_fields):
    """One result decoder per method; an empty Result table resolves to undefined."""
    lines = []
    if not result_fields:
        return lines
    lines.append("napi_value decode" + base + "Result(napi_env env, const flatbuffers::Vector<uint8_t>* result)")
    lines.append("{")
    lines.append("    if (result == nullptr || result->size() == 0) return undefined(env);")
    lines.append("    const services::" + base + "Result* value = services::Get" + base + "Result(result->data());")
    lines.append("    napi_value decoded = object(env);")
    for name, field_type in result_fields:
        lines.append("    property(env, decoded, \"" + camel(name) + "\", " +
                     property_value(name, field_type, "value->" + name + "()") + ");")
    lines.append("    return decoded;")
    lines.append("}")
    lines.append("")
    return lines


def render_header(service_name, fbs_name, methods, events):
    lines = [
        "#pragma once",
        "",
        "#include \"service_proxy.h\"",
        "#include <node_api.h>",
        "",
        "#include <cstdint>",
        "#include <string>",
        "#include <vector>",
        "",
        "namespace node_media_service",
        "{",
        "",
        "// Generated by core/schemas/generate.py: core/schemas/" + fbs_name + ".fbs",
        "// Same-name proxy for the server-side Service (media-service's " + service_name + "); service_id = class name.",
        "class " + service_name + " : public ServiceProxy",
        "{",
        "public:",
        "    explicit " + service_name + "(Transport* transport);",
        "",
        "    const char* serviceId() const override { return \"" + service_name + "\"; }",
        "",
    ]
    for method, base, args, _ in methods:
        params = [(name, field_type, cpp_type(field_type)) for name, field_type in args]
        lines.append("    napi_value " + method + "(napi_env env" + ("" if not params else ", ") + signature(params) + ");")
        lines.append("")
    for event, base, _ in events:
        lines.append('    // subscribe' + base + ': subscribe to the "' + event + '" event; payload decoded to a JS object; single listener, dispose() unregisters.')
        lines.append('    napi_value subscribe' + base + '(napi_env env, napi_value callback);')
        lines.append('    void unsubscribe' + base + '(napi_env env);')
        lines.append('')
    if events:
        lines.append("private:")
        lines.append("")
        for event, base, _ in events:
            lines.append("    napi_ref m_" + snake(base) + "Callback = nullptr;")
            lines.append("    void dispatch" + base + "(napi_env env, const std::vector<uint8_t>& payload);")
        lines.append("")
    lines += [
        "};",
        "",
        "// Expose " + service_name + "'s invoke methods on target as N-API functions (returning Promises).",
        "void install" + service_name + "(napi_env env, napi_value target, napi_value owner, " + service_name + "* service);",
        "",
        "} // namespace node_media_service",
        "",
    ]
    return chr(10).join(lines)


def render_event_decode(event_base, fields):
    """Event payload -> JS object. A ulong field named handle is duplicated from the child pid into a Buffer."""
    has_handle = any(name == 'handle' and field_type == 'ulong' for name, field_type in fields)
    lines = []
    lines.append('napi_value decode' + event_base + 'Event(napi_env env' + (', Transport* transport' if has_handle else '') + ', const std::vector<uint8_t>& payload)')
    lines.append('{')
    lines.append('    if (payload.empty()) return undefined(env);')
    lines.append('    flatbuffers::Verifier verifier(payload.data(), payload.size());')
    lines.append('    if (!services::Verify' + event_base + 'EventBuffer(verifier)) return undefined(env);')
    lines.append('    const services::' + event_base + 'Event* value = services::Get' + event_base + 'Event(payload.data());')
    lines.append('    napi_value decoded = object(env);')
    for name, field_type in fields:
        if name == 'handle' and field_type == 'ulong':
            lines.append('    property(env, decoded, "' + camel(name) + '", handleBuffer(env, transport ? transport->childPid() : 0, value->' + name + '()));')
        else:
            if field_type == 'ulong':
                lines.append('    property(env, decoded, "' + camel(name) + '", bigint(env, value->' + name + '()));')
            else:
                lines.append('    property(env, decoded, "' + camel(name) + '", ' + property_value(name, field_type, 'value->' + name + '()') + ');')
    lines.append('    return decoded;')
    lines.append('}')
    lines.append('')
    return lines

def render_source(service_name, fbs_name, methods, events):
    snake_name = snake(service_name)
    lines = [
        "#include \"" + snake_name + ".h\"",
        "",
        "#include \"runtime/napi_util.h\"",
        "#include \"runtime/transport.h\"",
        "#include \"protos/" + fbs_name + ".h\"",
        "#include \"protos/service.h\"",
        "",
        "#include <cstring>",
        "#include <stdexcept>",
        "",
        "namespace node_media_service",
        "{",
        "",
        "namespace",
        "{",
        "",
    ]
    for method, base, args, result in methods:
        lines.extend(render_decode(base, result))
    for event, base, fields in events:
        lines.extend(render_event_decode(base, fields))

    for event, base, _ in events:
        lines.append('struct ' + service_name + base + 'DisposeContext')
        lines.append('{')
        lines.append('    ' + service_name + '* service;')
        lines.append('    bool disposed;')
        lines.append('};')
        lines.append('')
        lines.append('napi_value ' + snake_name + base + 'Dispose(napi_env env, napi_callback_info info)')
        lines.append('{')
        lines.append('    void* data = nullptr;')
        lines.append('    NMS_NAPI(napi_get_cb_info)(env, info, nullptr, nullptr, nullptr, &data);')
        lines.append('    ' + service_name + base + 'DisposeContext* context = static_cast<' + service_name + base + 'DisposeContext*>(data);')
        lines.append('    if (context != nullptr && !context->disposed)')
        lines.append('    {')
        lines.append('        context->disposed = true;')
        lines.append('        context->service->unsubscribe' + base + '(env);')
        lines.append('    }')
        lines.append('    napi_value result = nullptr;')
        lines.append('    NMS_NAPI(napi_get_undefined)(env, &result);')
        lines.append('    return result;')
        lines.append('}')
        lines.append('')
        lines.append('void ' + snake_name + base + 'DisposeFinalizer(napi_env, void* data, void*)')
        lines.append('{')
        lines.append('    delete static_cast<' + service_name + base + 'DisposeContext*>(data);')
        lines.append('}')
        lines.append('')
    lines += [
        "struct " + service_name + "MethodContext",
        "{",
        "    " + service_name + "* service;",
        "    const char* name;",
        "};",
        "",
        "void " + snake_name + "MethodFinalizer(napi_env, void* data, void*)",
        "{",
        "    delete static_cast<" + service_name + "MethodContext*>(data);",
        "}",
        "",
        "napi_value " + snake_name + "Method(napi_env env, napi_callback_info info)",
        "{",
        "    try",
        "    {",
        "        napi_value args[8] = {};",
        "        size_t count = 8;",
        "        void* data = nullptr;",
        "        NMS_NAPI(napi_get_cb_info)(env, info, &count, args, nullptr, &data);",
        "        " + service_name + "MethodContext* context = static_cast<" + service_name + "MethodContext*>(data);",
        "        const char* method = context->name;",
    ]
    for method, base, args, _ in methods:
        params = [(name, field_type, cpp_type(field_type)) for name, field_type in args]
        reads = [arg_read(field_type, index) for index, (name, field_type, _) in enumerate(params)]
        lines.append("        if (std::strcmp(method, \"" + method + "\") == 0)")
        lines.append("        {")
        if params:
            lines.append("            if (count < " + str(len(params)) + ") throw std::runtime_error(\"" +
                         service_name + "." + method + " requires " + str(len(params)) + " arguments\");")
            lines.append("            return context->service->" + method + "(env" +
                         ("" if not reads else ", " + ", ".join(reads)) + ");")
        else:
            lines.append("            return context->service->" + method + "(env);")
        lines.append("        }")
    for event, base, _ in events:
        lines.append('        if (std::strcmp(method, "subscribe' + base + '") == 0)')
        lines.append('        {')
        lines.append('            if (count < 1) throw std::runtime_error("' + service_name + '.subscribe' + base + ' requires 1 arguments");')
        lines.append('            return context->service->subscribe' + base + '(env, args[0]);')
        lines.append('        }')
    lines += [
        "        throw std::runtime_error(std::string(\"Unknown " + snake_name + " method: \") + method);",
        "    }",
        "    catch (const std::exception& error)",
        "    {",
        "        NMS_NAPI(napi_throw_error)(env, nullptr, error.what());",
        "        return nullptr;",
        "    }",
        "}",
        "",
        "void add" + service_name + "Method(napi_env env, napi_value target, napi_value owner, " +
        service_name + "* service, const char* name)",
        "{",
        "    " + service_name + "MethodContext* context = new " + service_name + "MethodContext();",
        "    context->service = service;",
        "    context->name = name;",
        "    napi_value function = nullptr;",
        "    NMS_NAPI(napi_create_function)(env, name, NAPI_AUTO_LENGTH, " + snake_name + "Method, context, &function);",
        "    NMS_NAPI(napi_add_finalizer)(env, function, context, " + snake_name + "MethodFinalizer, nullptr, nullptr);",
        "    keepOwner(env, function, owner);",
        "    property(env, target, name, function);",
        "}",
        "",
        "} // namespace",
        "",
        service_name + "::" + service_name + "(Transport* transport) : ServiceProxy(transport)",
        "{",
        "}",
        "",
    ]
    for event, base, fields in events:
        has_handle = any(name == 'handle' and field_type == 'ulong' for name, field_type in fields)
        lines.append('napi_value ' + service_name + '::subscribe' + base + '(napi_env env, napi_value callback)')
        lines.append('{')
        lines.append('    napi_valuetype type = napi_undefined;')
        lines.append('    NMS_NAPI(napi_typeof)(env, callback, &type);')
        lines.append('    if (type != napi_function)')
        lines.append('    {')
        lines.append('        throw std::runtime_error("Expected a ' + event + ' event callback");')
        lines.append('    }')
        lines.append('    if (m_' + snake(base) + 'Callback != nullptr)')
        lines.append('    {')
        lines.append('        NMS_NAPI(napi_delete_reference)(env, m_' + snake(base) + 'Callback);')
        lines.append('        m_' + snake(base) + 'Callback = nullptr;')
        lines.append('    }')
        lines.append('    NMS_NAPI(napi_create_reference)(env, callback, 1, &m_' + snake(base) + 'Callback);')
        lines.append('    m_transport->setEventCallback("' + service_name + '", "' + event + '",')
        lines.append('        [this](napi_env env, const std::vector<uint8_t>& payload)')
        lines.append('        {')
        lines.append('            dispatch' + base + '(env, payload);')
        lines.append('        });')
        lines.append('    ' + service_name + base + 'DisposeContext* context = new ' + service_name + base + 'DisposeContext();')
        lines.append('    context->service = this;')
        lines.append('    context->disposed = false;')
        lines.append('    napi_value dispose = nullptr;')
        lines.append('    NMS_NAPI(napi_create_function)(env, "dispose", NAPI_AUTO_LENGTH, ' + snake_name + base + 'Dispose, context, &dispose);')
        lines.append('    NMS_NAPI(napi_add_finalizer)(env, dispose, context, ' + snake_name + base + 'DisposeFinalizer, nullptr, nullptr);')
        lines.append('    keepOwner(env, dispose, callback);')
        lines.append('    napi_value result = object(env);')
        lines.append('    property(env, result, "dispose", dispose);')
        lines.append('    return result;')
        lines.append('}')
        lines.append('')
        lines.append('void ' + service_name + '::unsubscribe' + base + '(napi_env env)')
        lines.append('{')
        lines.append('    if (m_' + snake(base) + 'Callback != nullptr)')
        lines.append('    {')
        lines.append('        NMS_NAPI(napi_delete_reference)(env, m_' + snake(base) + 'Callback);')
        lines.append('        m_' + snake(base) + 'Callback = nullptr;')
        lines.append('    }')
        lines.append('    m_transport->setEventCallback("' + service_name + '", "' + event + '", nullptr);')
        lines.append('}')
        lines.append('')
        lines.append('void ' + service_name + '::dispatch' + base + '(napi_env env, const std::vector<uint8_t>& payload)')
        lines.append('{')
        lines.append('    if (m_' + snake(base) + 'Callback == nullptr) return;')
        lines.append('    napi_value decoded = decode' + base + 'Event(env' + (', m_transport' if has_handle else '') + ', payload);')
        lines.append('    napi_value callback = nullptr;')
        lines.append('    NMS_NAPI(napi_get_reference_value)(env, m_' + snake(base) + 'Callback, &callback);')
        lines.append('    napi_value global = nullptr;')
        lines.append('    NMS_NAPI(napi_get_global)(env, &global);')
        lines.append('    napi_value argv[] = {decoded};')
        lines.append('    napi_value ignored = nullptr;')
        lines.append('    NMS_NAPI(napi_call_function)(env, global, callback, 1, argv, &ignored);')
        lines.append('}')
        lines.append('')
    for method, base, args, result in methods:
        params = [(name, field_type, cpp_type(field_type)) for name, field_type in args]
        lines.append("napi_value " + service_name + "::" + method + "(napi_env env" +
                     ("" if not params else ", ") + signature(params) + ")")
        lines.append("{")
        lines.append("    flatbuffers::FlatBufferBuilder builder;")
        prelude, encode_args = encode_exprs("builder", args, "    ")
        lines.extend(prelude)
        lines.append("    const auto args = services::Create" + base + "Args(builder" +
                     ("" if not encode_args else ", ") + ", ".join(encode_args) + ");")
        lines.append("    builder.Finish(args);")
        lines.append("")
        lines += [
            "    napi_deferred deferred = nullptr;",
            "    napi_value promise = nullptr;",
            "    NMS_NAPI(napi_create_promise)(env, &deferred, &promise);",
            "    m_transport->invoke(\"" + service_name + "\", \"" + method + "\",",
            "        builder.GetBufferPointer(), builder.GetSize(),",
            "        env, deferred,",
            "        [](napi_env env, napi_deferred deferred, const services::InvokeRsp* reply)",
            "        {",
            "            if (!reply->success())",
            "            {",
            "                const std::string message = reply->message() ? reply->message()->str() : \"" +
            service_name + "." + method + " failed\";",
            "                NMS_NAPI(napi_reject_deferred)(env, deferred, exception(env, message));",
            "                return;",
            "            }",
        ]
        if result:
            lines.append("            NMS_NAPI(napi_resolve_deferred)(env, deferred, decode" + base +
                         "Result(env, reply->result()));")
        else:
            lines.append("            NMS_NAPI(napi_resolve_deferred)(env, deferred, undefined(env));")
        lines += [
            "        });",
            "    return promise;",
            "}",
            "",
        ]
    lines += [
        "void install" + service_name + "(napi_env env, napi_value target, napi_value owner, " +
        service_name + "* service)",
        "{",
    ]
    for method, base, args, _ in methods:
        lines.append("    add" + service_name + "Method(env, target, owner, service, \"" + method + "\");")
    for event, base, _ in events:
        lines.append("    add" + service_name + "Method(env, target, owner, service, \"subscribe" + base + "\");")
    lines += [
        "}",
        "",
        "} // namespace node_media_service",
        "",
    ]
    return chr(10).join(lines)


def write_cpp(path, content):
    """Project convention: C++ files are UTF-8 BOM + CRLF."""
    body = content.replace("\n", "\r\n").encode("utf-8")
    path.write_bytes(b"\xef\xbb\xbf" + body)


def schema_roots(text):
    return re.findall(r"^\s*root_type\s+([A-Za-z_][A-Za-z0-9_]*)\s*;", text, re.M)


def strip_root_types(text):
    return re.sub(r"^\s*root_type\s+[A-Za-z_][A-Za-z0-9_]*\s*;.*$", "", text, flags=re.M)


def run_flatc(schema_text, root, out_dir):
    """Write a temporary schema and generate a header into out_dir."""
    fd, tmp_path = tempfile.mkstemp(suffix=".fbs", dir=str(SCHEMA_DIR))
    produced = None
    try:
        with open(fd, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(schema_text)
            if root:
                handle.write("\nroot_type %s;\n" % root)
        tmp = Path(tmp_path)
        subprocess.run([str(FLATC), "--cpp", "-o", str(out_dir), str(tmp)], check=True)
        produced = out_dir / (tmp.stem + "_generated.h")
    finally:
        try:
            Path(tmp_path).unlink()
        except OSError:
            pass
    return produced


def extract_root_block(content):
    """Root functions run from the first Get function to the namespace close."""
    start = content.index("\ninline const services::")
    end = content.index(NAMESPACE_CLOSE)
    block = content[start + 1 : end]
    if "}  // namespace services" in block:
        sys.exit("unexpected namespace close inside root block")
    return block


def write_header(path, merged):
    # BOM + CRLF, matching the other generated C++ headers in the project.
    data = merged.encode("utf-8")
    data = b"\xef\xbb\xbf" + data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
    path.write_bytes(data)


def generate_protos():
    if not FLATC.exists():
        sys.exit("flatc not found: %s" % FLATC)
    PROTOS_DIR.mkdir(parents=True, exist_ok=True)
    for schema_path in sorted(SCHEMA_DIR.glob("*.fbs")):
        text = schema_path.read_text(encoding="utf-8")
        roots = schema_roots(text)
        if not roots:
            print("no root_type in %s; skipping" % schema_path.name)
            continue
        stripped = strip_root_types(text)
        header_name = schema_path.name[: -len(".fbs")] + ".h"
        guard_name = "FLATBUFFERS_GENERATED_%s_SERVICES_H_" % schema_path.stem.upper()

        # 1. Table definitions only (no root_type).
        base = run_flatc(stripped, None, PROTOS_DIR)
        if base is None or not base.exists():
            sys.exit("flatc produced no base header for %s" % schema_path.name)
        base_text = base.read_text(encoding="utf-8")
        base.unlink()
        base_text = re.sub(
            r"FLATBUFFERS_GENERATED_[A-Z0-9_]+_SERVICES_H_",
            guard_name,
            base_text,
        )

        # 2. Root function blocks, one per root_type.
        blocks = []
        for root in roots:
            produced = run_flatc(stripped, root, PROTOS_DIR)
            if produced is None or not produced.exists():
                sys.exit("flatc produced no header for %s" % root)
            content = produced.read_text(encoding="utf-8")
            produced.unlink()
            blocks.append(extract_root_block(content))

        # 3. Insert all root blocks before the namespace close.
        merged = base_text.replace(
            NAMESPACE_CLOSE,
            "\n\n" + "\n\n".join(blocks) + NAMESPACE_CLOSE,
            1,
        )
        write_header(PROTOS_DIR / header_name, merged)
        print("regenerated %s (%d roots)" % (header_name, len(roots)))
    print("protos done")


def generate_services():
    written = []
    entries = []
    SERVICES_DIR.mkdir(parents=True, exist_ok=True)
    for schema in sorted(SCHEMA_DIR.glob("*_service.fbs")):
        text = schema.read_text(encoding="utf-8")
        tables = parse_tables(text)
        methods = collect_methods(tables)
        roots = schema_roots(text)
        events = collect_events(tables, roots)
        if not methods and not events:
            continue
        stem = schema.stem
        service_name = pascal(stem[: -len("_service")]) + "Service"
        header = SERVICES_DIR / (stem + ".h")
        source = SERVICES_DIR / (stem + ".cpp")
        write_cpp(header, render_header(service_name, stem, methods, events))
        write_cpp(source, render_source(service_name, stem, methods, events))
        written.append(header)
        written.append(source)
        entries.append((stem, service_name, events))
    generate_service_registry(entries)
    written.append(SERVICES_DIR / "service_registry.h")
    written.append(SERVICES_DIR / "service_registry.cpp")
    print("services done (%d files)" % len(written))
    return written


def generate_service_registry(entries):
    """Generate services/service_registry.{h,cpp}: unified ServiceFactory table + installAllServices.

    media-service 侧对应 services/register.{h,cpp}：业务服务只登记一行，application 不感知。
    node 侧宿主持有全部 ServiceProxy 实例；installAllServices 按 serviceId 匹配实例，
    在 root 上以 camel(serviceId)（如 nativeWindowService）挂出服务对象，方法为 invoke 方法。
    """
    lines = [
        '#pragma once',
        '',
        '#include "service_proxy.h"',
        '#include <node_api.h>',
        '',
        '#include <cstddef>',
        '#include <vector>',
        '',
        'namespace node_media_service',
        '{',
        '',
        '// Generated by core/schemas/generate.py: unified service factory table + installer.',
        '// 对应 media-service 的 services/register.{h,cpp}：新增服务只需在 fbs 里登记，',
        '// service_host 不感知具体服务。',
        'struct ServiceFactory',
        '{',
        '    const char* serviceId;                        // = proxy->serviceId()（类名）',
        '    const char* propertyName;                     // root 属性名 = camel(serviceId)，如 "nativeWindowService"',
        '    ServiceProxy* (*create)(Transport* transport);',
        '    void (*install)(napi_env env, napi_value target, napi_value owner, ServiceProxy* service);',
        '};',
        '',
        'const ServiceFactory* serviceFactories();',
        'std::size_t serviceFactoryCount();',
        '',
        '// 把每个服务代理挂到 root：属性名 = camel(serviceId)，方法 = 该服务的 invoke 方法（返回 Promise）。',
        '// services 为宿主持有的实例集合；未创建的实例自动跳过。',
        'void installAllServices(napi_env env, napi_value root, napi_value owner,',
        '                        const std::vector<ServiceProxy*>& services);',
        '',
        '} // namespace node_media_service',
        '',
        '',
    ]
    write_cpp(SERVICES_DIR / "service_registry.h", chr(10).join(lines))

    lines = [
        '#include "services/service_registry.h"',
        '',
    ]
    for stem, service_name, _ in entries:
        lines.append('#include "services/' + stem + '.h"')
    lines += [
        '',
        '#include "runtime/napi_util.h"',
        '',
        '#include <cstring>',
        '',
        'namespace node_media_service',
        '{',
        'namespace',
        '{',
        '',
    ]
    for stem, service_name, _ in entries:
        lines.append('ServiceProxy* create' + service_name + '(Transport* transport)')
        lines.append('{')
        lines.append('    return new ' + service_name + '(transport);')
        lines.append('}')
        lines.append('')
        lines.append('void install' + service_name + 'Proxy(napi_env env, napi_value target, napi_value owner, ServiceProxy* service)')
        lines.append('{')
        lines.append('    install' + service_name + '(env, target, owner, static_cast<' + service_name + '*>(service));')
        lines.append('}')
        lines.append('')
    lines += [
        '// 全部内置服务的工厂表：serviceId = 代理 serviceId()（类名），与 media-service 侧一致。',
        'const ServiceFactory kServiceFactories[] = {',
    ]
    for stem, service_name, _ in entries:
        lines.append('    {"' + service_name + '", "' + camel(service_name) + '", create' + service_name + ', install' + service_name + 'Proxy},')
    lines += [
        '};',
        '',
        '} // namespace',
        '',
        'const ServiceFactory* serviceFactories()',
        '{',
        '    return kServiceFactories;',
        '}',
        '',
        'std::size_t serviceFactoryCount()',
        '{',
        '    return sizeof(kServiceFactories) / sizeof(kServiceFactories[0]);',
        '}',
        '',
        'void installAllServices(napi_env env, napi_value root, napi_value owner, const std::vector<ServiceProxy*>& services)',
        '{',
        '    for (std::size_t index = 0; index < serviceFactoryCount(); ++index)',
        '    {',
        '        const ServiceFactory& factory = serviceFactories()[index];',
        '        ServiceProxy* service = nullptr;',
        '        for (ServiceProxy* candidate : services)',
        '        {',
        '            if (std::strcmp(candidate->serviceId(), factory.serviceId) == 0)',
        '            {',
        '                service = candidate;',
        '                break;',
        '            }',
        '        }',
        '        if (service == nullptr)',
        '        {',
        '            continue;',
        '        }',
        '        napi_value serviceObject = object(env);',
        '        factory.install(env, serviceObject, owner, service);',
        '        keepOwner(env, serviceObject, owner);',
        '        property(env, root, factory.propertyName, serviceObject);',
    ]
    lines += [
        '    }',
        '}',
        '',
        '} // namespace node_media_service',
        '',
        '',
    ]
    write_cpp(SERVICES_DIR / "service_registry.cpp", chr(10).join(lines))
    print("service_registry done (%d services)" % len(entries))


def generate_traits():
    """Emit core/common/protos/schema_traits.h: uniform verify/get for every root table.

    media-service 的 ServiceBase<Derived> 用它做"按方法名路由 + 解码 Args + 校验字节"：
    每个 root 表都有 flatc 生成的 Verify<Table>Buffer / Get<Table> 自由函数。
    所有特化形态完全一致，这里在文件内定义一个宏 FOLIO_SCHEMA_ROOT_TABLE 统一展开，
    新增/删除 root 表只需增删一行宏调用；宏在文件末尾 #undef，不污染其他翻译单元。
    """
    PROTOS_DIR.mkdir(parents=True, exist_ok=True)
    roots = []  # (schema stem, table name)，保持 schema 文件内 root_type 顺序
    seen = set()
    for schema in sorted(SCHEMA_DIR.glob("*.fbs")):
        text = schema.read_text(encoding="utf-8")
        for root in schema_roots(text):
            key = (schema.stem, root)
            if key not in seen:
                seen.add(key)
                roots.append(key)
    lines = [
        "#pragma once",
        "",
        "// Generated by core/schemas/generate.py. Do not edit.",
        "// Root-table traits: uniform verify/get access for every root table across core/schemas.",
        "// Used by media-service's ServiceBase<Derived> typed method dispatch (invoke/emit).",
        "",
    ]
    for stem in sorted({stem for stem, _ in roots}):
        lines.append('#include "protos/' + stem + '.h"')
    cont = "\\"
    lines += [
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace services",
        "{",
        "namespace traits",
        "{",
        "",
        "// 未特化的 Root 表示该类型不是 root 表（flatc 只为 root 生成 Verify/Get 自由函数）。",
        "template <typename T>",
        "struct Root;",
        "",
        "// 每个 root 表的特化形态完全一致，用文件内宏展开：",
        "// 新增/删除 root 表只需增删一行 FOLIO_SCHEMA_ROOT_TABLE(<表名>)。",
        "#define FOLIO_SCHEMA_ROOT_TABLE(ROOT)                     " + cont,
        "    template <>                                          " + cont,
        "    struct Root<ROOT>                                    " + cont,
        "    {                                                    " + cont,
        "        static const char* name() { return #ROOT; }      " + cont,
        "        static bool verify(const std::uint8_t* data, std::size_t size) " + cont,
        "        {                                                " + cont,
        "            flatbuffers::Verifier verifier(data, size);  " + cont,
        "            return Verify##ROOT##Buffer(verifier);       " + cont,
        "        }                                                " + cont,
        "        static const ROOT* root(const std::uint8_t* data) " + cont,
        "        {                                                " + cont,
        "            return Get##ROOT(data);                      " + cont,
        "        }                                                " + cont,
        "    };",
        "",
    ]
    for _, root in roots:
        lines.append("FOLIO_SCHEMA_ROOT_TABLE(" + root + ")")
    lines += [
        "",
        "#undef FOLIO_SCHEMA_ROOT_TABLE",
        "",
        "} // namespace traits",
        "} // namespace services",
        "",
    ]
    write_header(PROTOS_DIR / "schema_traits.h", chr(10).join(lines))
    print("traits done (%d roots)" % len(roots))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="only check whether generated outputs are current")
    args = parser.parse_args()

    before = {}
    for pattern in ("*.h", "*.cpp"):
        for path in sorted(SERVICES_DIR.glob(pattern)):
            before[path] = path.read_bytes()
    for path in sorted(PROTOS_DIR.glob("*.h")):
        before[path] = path.read_bytes()

    generate_protos()
    generate_traits()
    generate_services()

    if args.check:
        changed = [name for name, data in before.items() if name.read_bytes() != data]
        if changed:
            print("generate: stale outputs: " + ", ".join(str(p) for p in changed), file=sys.stderr)
            return 1
    print("generate: protocol headers and service proxies are up to date")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
