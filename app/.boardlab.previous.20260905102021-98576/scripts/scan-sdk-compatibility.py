#!/usr/bin/env python3
"""Read-only compatibility inventory for BoardLab-enabled Rockchip SDKs."""

from __future__ import print_function

import argparse
import collections
import hashlib
import json
import os
import re
import sys


DEFAULT_LINUX_ROOT = "/home/topeet/Linux"
MARKER = os.path.join(
    "buildroot", "package", "rockchip", "boardlab", "boardlab.mk"
)
PACKAGE_RELATIVE = os.path.join("package", "rockchip", "boardlab")
SKIP_SCAN_DIRS = frozenset((".git", ".repo", "__pycache__"))
SOURCE_EXCLUDED_ROOT_ENTRIES = frozenset(
    (
        ".git",
        ".gitignore",
        ".agents",
        ".claude",
        ".codex",
        ".hallmark",
        ".playwright-cli",
        "build",
        "build-host-ui",
        "output",
        "tmp",
    )
)
SOURCE_EXCLUDED_ROOT_PREFIXES = (
    "build-",
    "build_",
    "cmake-build-",
)
SKIP_OUTPUT_DIRS = frozenset(
    ("build", "host", "images", "legal-info", "per-package", "staging", "target")
)
ASSIGNMENT_RE = re.compile(
    r"^\s*(?:(?:export|override)\s+)?([A-Za-z0-9_]+)\s*"
    r"(\+=|:=|\?=|=)\s*(.*?)\s*$"
)
VARIABLE_RE = re.compile(r"\$\(([_A-Za-z0-9]+)\)|\$\{([_A-Za-z0-9]+)\}")
INCLUDE_RE = re.compile(r'^\s*#include\s+["<]([^">]+)[">]', re.MULTILINE)
PACKAGE_PAYLOAD_RE = re.compile(
    r"\$\(BOARDLAB_PKGDIR\)/([A-Za-z0-9_./+-]+)"
)
OPTIONAL_PACKAGE_PAYLOAD_RE = re.compile(
    r"\[\s+-f\s+\$\(BOARDLAB_PKGDIR\)/([A-Za-z0-9_./+-]+)\s*\]"
)


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except (IOError, OSError):
        return ""


def logical_make_lines(text):
    lines = []
    pending = ""
    for physical in text.splitlines():
        stripped = physical.rstrip()
        if stripped.endswith("\\"):
            pending += stripped[:-1] + " "
            continue
        lines.append(pending + stripped)
        pending = ""
    if pending:
        lines.append(pending)
    return lines


def make_variables(path):
    variables = {}
    for line in logical_make_lines(read_text(path)):
        match = ASSIGNMENT_RE.match(line)
        if not match:
            continue
        name, operator, value = match.groups()
        value = re.sub(r"\s+#.*$", "", value).strip()
        if operator == "+=" and name in variables:
            variables[name] = (variables[name] + " " + value).strip()
        elif operator != "?=" or name not in variables:
            variables[name] = value
    return variables


def expand_make_value(value, variables):
    current = value or ""
    for _ in range(12):

        def replace(match):
            name = match.group(1) or match.group(2)
            return variables.get(name, match.group(0))

        expanded = VARIABLE_RE.sub(replace, current)
        if expanded == current:
            break
        current = expanded
    return current.strip()


def make_value(path, name, overrides=None):
    variables = make_variables(path)
    if overrides:
        variables.update(overrides)
    return expand_make_value(variables.get(name, ""), variables)


def numeric_version(value):
    if not value:
        return ()
    match = re.search(r"\d+(?:\.\d+)+", value)
    if not match:
        return ()
    return tuple(int(part) for part in match.group(0).split("."))


def version_at_least(available, required):
    available_parts = numeric_version(available)
    required_parts = numeric_version(required)
    if not available_parts or not required_parts:
        return None
    length = max(len(available_parts), len(required_parts))
    available_parts += (0,) * (length - len(available_parts))
    required_parts += (0,) * (length - len(required_parts))
    return available_parts >= required_parts


def kernel_version(sdk_root):
    variables = make_variables(os.path.join(sdk_root, "kernel", "Makefile"))
    parts = [variables.get("VERSION", ""), variables.get("PATCHLEVEL", "")]
    if not all(parts):
        return "unknown"
    if variables.get("SUBLEVEL", ""):
        parts.append(variables["SUBLEVEL"])
    version = ".".join(parts)
    extra = expand_make_value(variables.get("EXTRAVERSION", ""), variables)
    return version + extra


def buildroot_version(buildroot):
    value = make_value(os.path.join(buildroot, "Makefile"), "BR2_VERSION")
    return value if value and "$(" not in value and "${" not in value else "unknown"


def walk_sdk_roots(linux_root):
    roots = []
    errors = []
    pending = [os.path.abspath(linux_root)]
    while pending:
        directory = pending.pop()
        marker = os.path.join(directory, MARKER)
        if os.path.isfile(marker):
            roots.append(directory)
            continue
        try:
            children = list(os.scandir(directory))
        except OSError as error:
            errors.append("{}: {}".format(directory, error))
            continue
        for child in children:
            if child.name in SKIP_SCAN_DIRS:
                continue
            try:
                if child.is_dir(follow_symlinks=False):
                    pending.append(child.path)
                elif child.is_symlink() and os.path.isfile(os.path.join(child.path, MARKER)):
                    roots.append(os.path.abspath(child.path))
            except OSError as error:
                errors.append("{}: {}".format(child.path, error))
    return sorted(set(roots)), sorted(set(errors))


def all_files(root, excluded_root_entries=(), excluded_root_prefixes=()):
    result = []
    if not os.path.isdir(root):
        return result
    root = os.path.abspath(root)
    excluded = set(excluded_root_entries)
    for directory, directories, files in os.walk(root, followlinks=False):
        at_root = os.path.abspath(directory) == root
        directories[:] = sorted(
            name
            for name in directories
            if name not in SKIP_SCAN_DIRS
            and (
                not at_root
                or (
                    name not in excluded
                    and not any(
                        name.startswith(prefix) for prefix in excluded_root_prefixes
                    )
                )
            )
        )
        for name in sorted(files):
            if (
                name.endswith(".pyc")
                or name == ".DS_Store"
                or (at_root and name in excluded)
            ):
                continue
            path = os.path.join(directory, name)
            if os.path.isfile(path):
                result.append(os.path.relpath(path, root).replace(os.sep, "/"))
    return sorted(result)


def file_hash(path):
    digest = hashlib.sha256()
    try:
        with open(path, "rb") as handle:
            while True:
                block = handle.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
    except (IOError, OSError):
        return ""
    return digest.hexdigest()


def manifest_hash(root, relative_files):
    digest = hashlib.sha256()
    for relative in sorted(relative_files):
        path = os.path.join(root, relative.replace("/", os.sep))
        content_hash = file_hash(path)
        digest.update(relative.encode("utf-8", "surrogateescape"))
        digest.update(b"\0")
        digest.update((content_hash or "MISSING").encode("ascii"))
        digest.update(b"\0")
    return digest.hexdigest()


def tree_manifest(root, excluded_root_entries=(), excluded_root_prefixes=()):
    return {
        relative: file_hash(os.path.join(root, relative.replace("/", os.sep)))
        for relative in all_files(
            root, excluded_root_entries, excluded_root_prefixes
        )
    }


def manifest_map_hash(manifest):
    digest = hashlib.sha256()
    for relative in sorted(manifest):
        digest.update(relative.encode("utf-8", "surrogateescape"))
        digest.update(b"\0")
        digest.update((manifest[relative] or "UNREADABLE").encode("ascii"))
        digest.update(b"\0")
    return digest.hexdigest()


def compare_source_trees(canonical_root, sdk_root, canonical_manifest, canonical_hash):
    sdk_manifest = tree_manifest(
        sdk_root, SOURCE_EXCLUDED_ROOT_ENTRIES, SOURCE_EXCLUDED_ROOT_PREFIXES
    )
    canonical_paths = set(canonical_manifest)
    sdk_paths = set(sdk_manifest)
    missing = sorted(canonical_paths - sdk_paths)
    extra = sorted(sdk_paths - canonical_paths)
    changed = sorted(
        relative
        for relative in canonical_paths & sdk_paths
        if canonical_manifest[relative] != sdk_manifest[relative]
    )
    return {
        "canonical_path": canonical_root,
        "sdk_path": sdk_root,
        "hash_scope": "distributable-source-files",
        "excluded_root_entries": sorted(SOURCE_EXCLUDED_ROOT_ENTRIES),
        "excluded_root_prefixes": list(SOURCE_EXCLUDED_ROOT_PREFIXES),
        "canonical_hash": canonical_hash,
        "sdk_hash": manifest_map_hash(sdk_manifest),
        "canonical_file_count": len(canonical_manifest),
        "sdk_file_count": len(sdk_manifest),
        "drift": bool(changed or missing or extra),
        "changed_files": changed,
        "missing_files": missing,
        "extra_files": extra,
    }


def compare_packages(canonical_package, sdk_package, canonical_files, canonical_hash):
    changed = []
    missing = []
    for relative in canonical_files:
        canonical_path = os.path.join(canonical_package, relative.replace("/", os.sep))
        sdk_path = os.path.join(sdk_package, relative.replace("/", os.sep))
        if not os.path.isfile(sdk_path):
            missing.append(relative)
        elif file_hash(canonical_path) != file_hash(sdk_path):
            changed.append(relative)
    sdk_files = all_files(sdk_package)
    extra = sorted(set(sdk_files) - set(canonical_files))
    sdk_hash = manifest_hash(sdk_package, canonical_files)
    return {
        "canonical_path": canonical_package,
        "sdk_path": sdk_package,
        "hash_scope": "canonical-files",
        "canonical_hash": canonical_hash,
        "sdk_hash": sdk_hash,
        "sdk_full_hash": manifest_hash(sdk_package, sdk_files),
        "drift": bool(changed or missing),
        "changed_files": changed,
        "missing_files": missing,
        "extra_files": extra,
    }


def config_raw_value(text, symbol):
    value = ""
    assignment = symbol + "="
    not_set = "# {} is not set".format(symbol)
    reset = "# {} is reset to default".format(symbol)
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(assignment):
            value = stripped[len(assignment):].strip()
        elif stripped == not_set or stripped == reset:
            value = ""
    return value


def config_enabled(text, symbol):
    return config_raw_value(text, symbol) == "y"


def config_string(text, symbol):
    value = config_raw_value(text, symbol)
    if len(value) >= 2 and value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    return value


def kconfig_selects(text, symbol):
    return bool(
        re.search(
            r"^\s*select\s+{}(?:\s|$)".format(re.escape(symbol)),
            text,
            re.MULTILINE,
        )
    )


def kconfig_depends_on(text, symbol):
    return bool(
        re.search(
            r"^\s*depends\s+on\s+{}(?:\s|$)".format(re.escape(symbol)),
            text,
            re.MULTILINE,
        )
    )


def required_package_payloads(package_make):
    required = set()
    for line in logical_make_lines(package_make):
        references = set(PACKAGE_PAYLOAD_RE.findall(line))
        optional = set(OPTIONAL_PACKAGE_PAYLOAD_RE.findall(line))
        required.update(references - optional)
    return sorted(required)


def config_file_index(config_root):
    contents = {}
    if not os.path.isdir(config_root):
        return contents
    for directory, directories, files in os.walk(config_root, followlinks=False):
        directories[:] = sorted(
            name for name in directories if name not in SKIP_SCAN_DIRS
        )
        for name in files:
            path = os.path.join(directory, name)
            try:
                if os.path.isfile(path) and os.path.getsize(path) <= 2 * 1024 * 1024:
                    contents[os.path.abspath(path)] = read_text(path)
            except OSError:
                continue
    return contents


def resolve_config_include(source, include, config_root, indexed_paths):
    candidates = (
        os.path.join(os.path.dirname(source), include),
        os.path.join(config_root, include),
        os.path.join(config_root, "rockchip", include),
    )
    for candidate in candidates:
        absolute = os.path.abspath(candidate)
        if absolute in indexed_paths:
            return absolute
    normalized = include.replace("\\", "/").lstrip("./")
    matches = []
    for path in indexed_paths:
        relative = os.path.relpath(path, config_root).replace(os.sep, "/")
        if relative == normalized or relative.endswith("/" + normalized):
            matches.append(path)
    return sorted(matches)[0] if len(matches) == 1 else ""


def effective_output_configs(buildroot):
    candidates = []
    direct = os.path.join(buildroot, ".config")
    if config_enabled(read_text(direct), "BR2_PACKAGE_BOARDLAB"):
        candidates.append(direct)

    output_root = os.path.join(buildroot, "output")
    if os.path.isdir(output_root):
        for directory, directories, files in os.walk(output_root, followlinks=False):
            directories[:] = sorted(
                name
                for name in directories
                if name not in SKIP_OUTPUT_DIRS and name not in SKIP_SCAN_DIRS
            )
            if ".config" not in files:
                continue
            path = os.path.join(directory, ".config")
            if config_enabled(read_text(path), "BR2_PACKAGE_BOARDLAB"):
                candidates.append(path)
    return sorted(set(os.path.abspath(path) for path in candidates))


def boardlab_config_context(buildroot):
    effective = effective_output_configs(buildroot)
    if effective:
        return {
            "kind": "effective-config",
            "sources": [os.path.relpath(path, buildroot) for path in effective],
            "variants": [
                {
                    "source": os.path.relpath(path, buildroot),
                    "text": read_text(path),
                }
                for path in effective
            ],
        }

    config_root = os.path.join(buildroot, "configs")
    contents = config_file_index(config_root)
    if not contents:
        return {"kind": "missing", "sources": [], "variants": []}

    indexed_paths = set(contents)
    reverse_includes = collections.defaultdict(set)
    for source, text in contents.items():
        for include in INCLUDE_RE.findall(text):
            target = resolve_config_include(source, include, config_root, indexed_paths)
            if target:
                reverse_includes[target].add(source)

    fragments = set(
        path
        for path, text in contents.items()
        if config_enabled(text, "BR2_PACKAGE_BOARDLAB")
    )
    reachable = set(fragments)
    pending = list(fragments)
    while pending:
        target = pending.pop()
        for parent in reverse_includes.get(target, ()):
            if parent not in reachable:
                reachable.add(parent)
                pending.append(parent)

    roots = sorted(
        path for path in reachable if os.path.basename(path).endswith("defconfig")
    )
    if not roots:
        roots = sorted(fragments)

    cache = {}

    def expand(path, stack):
        if path in cache:
            return cache[path]
        if path in stack:
            return ""
        stack = set(stack)
        stack.add(path)
        chunks = []
        for line in contents.get(path, "").splitlines():
            match = re.match(r'^\s*#include\s+["<]([^">]+)[">]', line)
            if match:
                target = resolve_config_include(
                    path, match.group(1), config_root, indexed_paths
                )
                if target:
                    chunks.append(expand(target, stack))
            else:
                chunks.append(line)
        cache[path] = "\n".join(chunks)
        return cache[path]

    variants = []
    for path in roots:
        text = expand(path, set())
        if config_enabled(text, "BR2_PACKAGE_BOARDLAB"):
            variants.append(
                {"source": os.path.relpath(path, buildroot), "text": text}
            )
    return {
        "kind": "expanded-defconfig" if variants else "missing",
        "sources": [variant["source"] for variant in variants],
        "variants": variants,
    }


def architecture_from_config(text):
    explicit = config_string(text, "BR2_ARCH")
    if explicit:
        return explicit
    symbols = (
        ("BR2_aarch64", "aarch64"),
        ("BR2_aarch64_be", "aarch64_be"),
        ("BR2_arm", "arm"),
        ("BR2_x86_64", "x86_64"),
        ("BR2_riscv", "riscv"),
    )
    for symbol, label in symbols:
        if config_enabled(text, symbol):
            return label
    return "unknown"


def libc_from_config(text):
    symbols = (
        ("BR2_TOOLCHAIN_USES_GLIBC", "glibc"),
        ("BR2_TOOLCHAIN_BUILDROOT_GLIBC", "glibc"),
        ("BR2_TOOLCHAIN_EXTERNAL_CUSTOM_GLIBC", "glibc"),
        ("BR2_TOOLCHAIN_USES_MUSL", "musl"),
        ("BR2_TOOLCHAIN_BUILDROOT_MUSL", "musl"),
        ("BR2_TOOLCHAIN_EXTERNAL_CUSTOM_MUSL", "musl"),
        ("BR2_TOOLCHAIN_USES_UCLIBC", "uClibc"),
        ("BR2_TOOLCHAIN_BUILDROOT_UCLIBC", "uClibc"),
        ("BR2_TOOLCHAIN_EXTERNAL_CUSTOM_UCLIBC", "uClibc"),
    )
    for symbol, label in symbols:
        if config_enabled(text, symbol):
            return label
    return "unknown"


def init_from_config(text, buildroot):
    symbols = (
        ("BR2_INIT_SYSTEMD", "systemd"),
        ("BR2_INIT_BUSYBOX", "busybox/SysV"),
        ("BR2_INIT_SYSV", "SysV"),
        ("BR2_INIT_NONE", "none"),
    )
    for symbol, label in symbols:
        if config_enabled(text, symbol):
            return label
    system_config = read_text(os.path.join(buildroot, "system", "Config.in"))
    if re.search(r"^\s*default\s+BR2_INIT_BUSYBOX(?:\s|$)", system_config, re.MULTILINE):
        return "busybox/SysV (default)"
    return "unknown"


def display_from_config(text, package_config):
    qt_wayland = config_enabled(text, "BR2_PACKAGE_QT5WAYLAND") or (
        "select BR2_PACKAGE_QT5WAYLAND" in package_config
    )
    weston = config_enabled(text, "BR2_PACKAGE_WESTON")
    if qt_wayland and weston:
        return "Wayland/Weston"
    if qt_wayland:
        return "Wayland (Weston unconfirmed)"
    if config_enabled(text, "BR2_PACKAGE_XORG7"):
        return "X11"
    return "unknown"


def find_named_makefile(package_root, basename, preferred):
    for relative in preferred:
        candidate = os.path.join(package_root, relative)
        if os.path.isfile(candidate):
            return candidate
    for directory, directories, files in os.walk(package_root, followlinks=False):
        directories[:] = sorted(
            name for name in directories if name not in SKIP_SCAN_DIRS
        )
        if basename in files:
            return os.path.join(directory, basename)
    return ""


def package_source(makefile, prefix, buildroot):
    if not makefile:
        return {
            "available": False,
            "path": "",
            "version": "unknown",
            "site": "unknown",
            "method": "unknown",
        }
    variables = make_variables(makefile)
    version = expand_make_value(variables.get(prefix + "_VERSION", ""), variables)
    site = expand_make_value(variables.get(prefix + "_SITE", ""), variables)
    method = expand_make_value(variables.get(prefix + "_SITE_METHOD", ""), variables)
    if not method:
        method = "github" if "$(call github" in site else "remote"
    return {
        "available": True,
        "path": os.path.relpath(makefile, buildroot).replace(os.sep, "/"),
        "version": version or "unknown",
        "site": site or "unknown",
        "method": method,
    }


def tree_contains(root, expression, names=None):
    if not os.path.isdir(root):
        return False
    pattern = re.compile(expression, re.MULTILINE)
    for directory, directories, files in os.walk(root, followlinks=False):
        directories[:] = sorted(
            name for name in directories if name not in SKIP_SCAN_DIRS
        )
        for name in files:
            if names and not any(name.startswith(prefix) for prefix in names):
                continue
            if pattern.search(read_text(os.path.join(directory, name))):
                return True
    return False


def application_requirements(application_root):
    cmake_lists = os.path.join(application_root, "CMakeLists.txt")
    text = read_text(cmake_lists)
    cmake_match = re.search(
        r"cmake_minimum_required\s*\(\s*VERSION\s+([0-9.]+)", text, re.IGNORECASE
    )
    qt_match = re.search(
        r"find_package\s*\(\s*Qt5\s+([0-9.]+)", text, re.IGNORECASE
    )
    return {
        "path": cmake_lists,
        "available": bool(text),
        "cmake": cmake_match.group(1) if cmake_match else "unknown",
        "qt5": qt_match.group(1) if qt_match else "unknown",
    }


def qt_version(buildroot, config_text):
    qt_makefile = os.path.join(buildroot, "package", "qt5", "qt5.mk")
    version_symbols = sorted(
        set(
            re.findall(
                r"^(BR2_PACKAGE_QT5_VERSION_(\d+)_(\d+))=",
                config_text,
                re.MULTILINE,
            )
        )
    )
    selected = [
        (major, minor)
        for symbol, major, minor in version_symbols
        if config_enabled(config_text, symbol)
    ]
    overrides = {}
    if len(selected) == 1:
        overrides["QT5_VERSION_MAJOR"] = "{}.{}".format(*selected[0])
    value = make_value(qt_makefile, "QT5_VERSION", overrides)
    return value if value and "$(" not in value and "${" not in value else "unknown"


def cmake_version(buildroot):
    path = os.path.join(buildroot, "package", "cmake", "cmake.mk")
    value = make_value(path, "CMAKE_VERSION")
    return value if value and "$(" not in value and "${" not in value else "unknown"


def rknn_provider(boardlab_makefile):
    text = "\n".join(logical_make_lines(read_text(boardlab_makefile)))
    match = re.search(
        r"-DBOARDLAB_RKNN_PROVIDER=(.*?)(?=\s+-D[A-Z0-9_]+=|$)", text
    )
    if not match:
        if "-DBOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME=ON" in text:
            return "BUNDLED"
        return "AUTO/system-sysroot"
    value = match.group(1)
    if "SYSTEM" in value and "OFF" in value:
        return "SYSTEM when NPU enabled; OFF otherwise"
    return value


def short_source(source):
    if not source["available"]:
        return "missing"
    return "{}@{}[{}]".format(
        os.path.basename(os.path.dirname(source["path"])),
        source["version"],
        source["method"],
    )


def summarize_config_values(values):
    ordered = []
    for value in values:
        if value not in ordered:
            ordered.append(value)
    if not ordered:
        return "unknown"
    if len(ordered) == 1:
        return ordered[0]
    return "mixed ({})".format(", ".join(ordered))


def analyze_sdk(sdk_root, linux_root, canonical):
    buildroot = os.path.join(sdk_root, "buildroot")
    sdk_package = os.path.join(buildroot, PACKAGE_RELATIVE)
    config = boardlab_config_context(buildroot)
    config_variants = config["variants"]
    config_texts = [variant["text"] for variant in config_variants]
    application = canonical["application_requirements"]
    package_make = canonical["package_make"]
    package_config = canonical["package_config"]
    canonical_dependencies = set(
        make_value(canonical["package_makefile"], "BOARDLAB_DEPENDENCIES").split()
    )
    required_payloads = canonical["required_package_payloads"]
    missing_canonical_payloads = canonical["missing_required_package_payloads"]
    missing_sdk_payloads = [
        relative
        for relative in required_payloads
        if not os.path.isfile(os.path.join(sdk_package, relative.replace("/", os.sep)))
    ]

    available_cmake = cmake_version(buildroot)
    qt_versions = [qt_version(buildroot, text) for text in config_texts]
    available_qt = summarize_config_values(qt_versions)
    architectures = [architecture_from_config(text) for text in config_texts]
    architecture = summarize_config_values(architectures)
    libcs = [libc_from_config(text) for text in config_texts]
    libc = summarize_config_values(libcs)
    init_systems = [init_from_config(text, buildroot) for text in config_texts]
    init_system = summarize_config_values(init_systems)
    displays = [display_from_config(text, package_config) for text in config_texts]
    display = summarize_config_values(displays)

    package_root = os.path.join(buildroot, "package")
    rknn_makefile = find_named_makefile(
        package_root,
        "rknpu2.mk",
        (os.path.join("rockchip", "rknpu2", "rknpu2.mk"),
         os.path.join("rknpu2", "rknpu2.mk")),
    )
    opencv_makefile = find_named_makefile(
        package_root,
        "opencv4.mk",
        (os.path.join("opencv4", "opencv4.mk"),
         os.path.join("rockchip", "opencv4", "opencv4.mk")),
    )
    rknn = package_source(rknn_makefile, "RKNPU2", buildroot)
    opencv = package_source(opencv_makefile, "OPENCV4", buildroot)
    npu_enabled_by_config = [
        config_enabled(text, "BR2_PACKAGE_BOARDLAB_NPU") for text in config_texts
    ]
    npu_enabled = any(npu_enabled_by_config)
    npu_dependencies = {"rknpu2", "opencv4"}.issubset(canonical_dependencies)

    gst_root = os.path.join(package_root, "gstreamer1", "gst1-plugins-base")
    gst_symbol = tree_contains(
        gst_root,
        r"^\s*config\s+BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_APP\s*$",
        ("Config.in",),
    )
    gst_selected = kconfig_selects(
        package_config, "BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_APP"
    )
    split_convert_symbols = all(
        tree_contains(
            gst_root,
            r"^\s*config\s+{}\s*$".format(re.escape(symbol)),
            ("Config.in",),
        )
        for symbol in (
            "BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOCONVERT",
            "BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOSCALE",
        )
    )
    combined_convert_symbol = tree_contains(
        gst_root,
        r"^\s*config\s+BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOCONVERTSCALE\s*$",
        ("Config.in",),
    )
    split_convert_selected = all(
        kconfig_selects(package_config, symbol)
        for symbol in (
            "BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOCONVERT",
            "BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOSCALE",
        )
    )
    combined_convert_selected = kconfig_selects(
        package_config,
        "BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOCONVERTSCALE",
    )
    split_version_selected_by_config = [
        config_enabled(text, "BR2_PACKAGE_GSTREAMER1_18")
        or config_enabled(text, "BR2_PACKAGE_GSTREAMER1_20")
        for text in config_texts
    ]
    split_version_selected = bool(split_version_selected_by_config) and all(
        split_version_selected_by_config
    )
    if split_convert_symbols and not combined_convert_symbol:
        convert_ready = split_convert_selected and split_version_selected
        convert_layout = "split"
    elif combined_convert_symbol and not split_convert_symbols:
        convert_ready = combined_convert_selected and bool(config_texts) and not any(
            split_version_selected_by_config
        )
        convert_layout = "combined"
    elif split_convert_symbols and combined_convert_symbol:
        convert_ready = bool(config_texts) and all(
            split_convert_selected if split_selected else combined_convert_selected
            for split_selected in split_version_selected_by_config
        )
        convert_layout = summarize_config_values(
            [
                "split" if split_selected else "combined"
                for split_selected in split_version_selected_by_config
            ]
        )
    else:
        convert_ready = False
        convert_layout = "missing"

    gst_good_root = os.path.join(package_root, "gstreamer1", "gst1-plugins-good")
    required_good_symbols = (
        "BR2_PACKAGE_GST1_PLUGINS_GOOD_JPEG",
        "BR2_PACKAGE_GST1_PLUGINS_GOOD_PLUGIN_MULTIFILE",
        "BR2_PACKAGE_GST1_PLUGINS_GOOD_PLUGIN_V4L2",
    )
    good_symbols_available = all(
        tree_contains(
            gst_good_root,
            r"^\s*config\s+{}\s*$".format(re.escape(symbol)),
            ("Config.in",),
        )
        for symbol in required_good_symbols
    )
    good_symbols_selected = all(
        kconfig_selects(package_config, symbol) for symbol in required_good_symbols
    )
    gst_required = "-DBOARDLAB_REQUIRE_GSTREAMER_APPSINK=ON" in package_make
    gst_dependency_names = ("gstreamer1", "gst1-plugins-base", "gst1-plugins-good")
    gst_dependencies_declared = set(gst_dependency_names).issubset(
        canonical_dependencies
    )
    gst_packages_available = all(
        os.path.isdir(os.path.join(package_root, "gstreamer1", dependency))
        for dependency in ("gst1-plugins-base", "gst1-plugins-good")
    ) and os.path.isdir(os.path.join(package_root, "gstreamer1", "gstreamer1"))
    gst_dependencies = gst_dependencies_declared and gst_packages_available
    gst_ready = (
        gst_symbol
        and gst_selected
        and gst_required
        and gst_dependencies
        and convert_ready
        and good_symbols_available
        and good_symbols_selected
    )

    util_linux_config = read_text(
        os.path.join(package_root, "util-linux", "Config.in")
    )
    storage_tool_symbols = all(
        re.search(
            r"^\s*(?:menu)?config\s+{}\s*$".format(re.escape(symbol)),
            util_linux_config,
            re.MULTILINE,
        )
        for symbol in (
            "BR2_PACKAGE_UTIL_LINUX",
            "BR2_PACKAGE_UTIL_LINUX_BINARIES",
        )
    )
    storage_tools_selected = all(
        kconfig_selects(package_config, symbol)
        for symbol in (
            "BR2_PACKAGE_UTIL_LINUX",
            "BR2_PACKAGE_UTIL_LINUX_BINARIES",
        )
    )
    storage_tools_dependency = "util-linux" in canonical_dependencies
    storage_tools_ready = (
        storage_tool_symbols and storage_tools_selected and storage_tools_dependency
    )

    qt_dependency_names = (
        "qt5base",
        "qt5declarative",
        "qt5quickcontrols2",
        "qt5wayland",
        "weston",
    )
    qt_dependencies_declared = set(qt_dependency_names).issubset(
        canonical_dependencies
    )
    qt_packages_available = all(
        os.path.isdir(os.path.join(package_root, "qt5", dependency))
        for dependency in qt_dependency_names
        if dependency != "weston"
    ) and os.path.isdir(os.path.join(package_root, "weston"))
    required_qt_symbols = (
        "BR2_PACKAGE_QT5BASE",
        "BR2_PACKAGE_QT5BASE_GUI",
        "BR2_PACKAGE_QT5BASE_OPENGL",
        "BR2_PACKAGE_QT5BASE_NETWORK",
        "BR2_PACKAGE_QT5BASE_CONCURRENT",
        "BR2_PACKAGE_QT5DECLARATIVE",
        "BR2_PACKAGE_QT5DECLARATIVE_QUICK",
        "BR2_PACKAGE_QT5QUICKCONTROLS2",
        "BR2_PACKAGE_QT5WAYLAND",
    )
    qt_symbol_availability = {
        symbol: tree_contains(
            os.path.join(package_root, "qt5"),
            r"^\s*(?:menu)?config\s+{}\s*$".format(re.escape(symbol)),
            ("Config.in",),
        )
        for symbol in required_qt_symbols
    }
    qt_symbols_available = all(qt_symbol_availability.values())
    qt_symbols_selected = all(
        kconfig_selects(package_config, symbol) for symbol in required_qt_symbols
    )
    qt_prerequisite_symbols = (
        "BR2_PACKAGE_QT5",
        "BR2_PACKAGE_QT5_JSCORE_AVAILABLE",
        "BR2_PACKAGE_QT5_GL_AVAILABLE",
        "BR2_PACKAGE_WESTON",
    )
    qt_prerequisites_declared = all(
        kconfig_depends_on(package_config, symbol)
        for symbol in qt_prerequisite_symbols
    )

    configuration_variants = []
    effective_qt_configurations_ready = True
    for index, variant in enumerate(config_variants):
        text = variant["text"]
        resolved = config["kind"] == "effective-config"
        missing_qt_symbols = []
        if resolved:
            missing_qt_symbols = [
                symbol
                for symbol in required_qt_symbols
                if not config_enabled(text, symbol)
            ]
            if missing_qt_symbols:
                effective_qt_configurations_ready = False
        variant_reasons = []
        if version_at_least(qt_versions[index], application["qt5"]) is not True:
            variant_reasons.append(
                "Qt {} does not prove the required {} baseline".format(
                    qt_versions[index], application["qt5"]
                )
            )
        if displays[index] != "Wayland/Weston":
            variant_reasons.append(
                "display path is {} rather than Wayland/Weston".format(
                    displays[index]
                )
            )
        if missing_qt_symbols:
            variant_reasons.append(
                "effective config is missing {}".format(
                    ", ".join(missing_qt_symbols)
                )
            )
        if missing_qt_symbols or version_at_least(
            qt_versions[index], application["qt5"]
        ) is not True:
            variant_status = "unsupported"
        elif displays[index] != "Wayland/Weston":
            variant_status = "ineligible"
        else:
            variant_status = "supported" if resolved else "declared-compatible"
        configuration_variants.append(
            {
                "source": variant["source"],
                "evidence": "resolved" if resolved else "declared",
                "qt": qt_versions[index],
                "architecture": architectures[index],
                "libc": libcs[index],
                "display": displays[index],
                "init": init_systems[index],
                "npu_enabled": npu_enabled_by_config[index],
                "gstreamer_layout": (
                    "split" if split_version_selected_by_config[index]
                    else "combined"
                ),
                "missing_effective_qt_symbols": missing_qt_symbols,
                "status": variant_status,
                "reasons": variant_reasons or [
                    "configuration satisfies the canonical static contract"
                ],
            }
        )

    package_registration = read_text(
        os.path.join(buildroot, "package", "rockchip", "Config.in")
    )
    package_registered = "boardlab/Config.in" in package_registration
    package_comparison = compare_packages(
        canonical["package_path"],
        sdk_package,
        canonical["package_files"],
        canonical["package_hash"],
    )
    package_comparison["required_payloads"] = required_payloads
    package_comparison["missing_required_payloads"] = missing_sdk_payloads
    application_root = os.path.join(sdk_root, "app", "boardlab")
    application_comparison = compare_source_trees(
        canonical["project_root"],
        application_root,
        canonical["source_manifest"],
        canonical["source_hash"],
    )

    fatal = []
    warnings = []
    kernel = kernel_version(sdk_root)
    buildroot_release = buildroot_version(buildroot)
    if kernel == "unknown":
        warnings.append("kernel version is not readable from kernel/Makefile")
    if buildroot_release == "unknown":
        warnings.append("Buildroot version is not readable from buildroot/Makefile")
    if not package_registered:
        fatal.append("Rockchip package menu does not source boardlab/Config.in")
    if not application["available"]:
        fatal.append("canonical CMakeLists.txt is missing or unreadable")
    if missing_canonical_payloads:
        fatal.append(
            "canonical boardlab.mk is missing required payloads: {}".format(
                ", ".join(missing_canonical_payloads)
            )
        )
    if missing_sdk_payloads:
        fatal.append(
            "SDK BoardLab package is missing required payloads: {}".format(
                ", ".join(missing_sdk_payloads)
            )
        )
    if not config["sources"]:
        warnings.append("no BoardLab-enabled Buildroot configuration was found")

    cmake_compatible = version_at_least(available_cmake, application["cmake"])
    if available_cmake == "unknown":
        fatal.append("Buildroot host CMake version is unknown")
    elif application["cmake"] == "unknown":
        warnings.append("canonical BoardLab minimum CMake version is unknown")
    elif cmake_compatible is False:
        fatal.append(
            "Buildroot CMake {} is below canonical BoardLab requirement {}".format(
                available_cmake, application["cmake"]
            )
        )

    qt_compatibilities = [
        version_at_least(version, application["qt5"]) for version in qt_versions
    ]
    qt_compatible = bool(qt_compatibilities) and all(
        compatible is True for compatible in qt_compatibilities
    )
    if not qt_versions or any(version == "unknown" for version in qt_versions):
        fatal.append("Buildroot Qt5 version is unknown")
    elif application["qt5"] != "unknown" and not qt_compatible:
        fatal.append(
            "one or more Buildroot Qt variants ({}) are below canonical BoardLab requirement {}".format(
                available_qt, application["qt5"]
            )
        )
    if not qt_dependencies_declared:
        fatal.append("canonical package does not declare every required Qt5/Weston dependency")
    if not qt_packages_available:
        fatal.append("SDK Buildroot is missing one or more required Qt5/Weston packages")
    if not qt_symbols_available:
        missing_symbols = sorted(
            symbol for symbol, available in qt_symbol_availability.items()
            if not available
        )
        fatal.append(
            "SDK Buildroot is missing required Qt5 Kconfig symbols: {}".format(
                ", ".join(missing_symbols)
            )
        )
    if not qt_symbols_selected:
        fatal.append("canonical package does not select every required Qt5 module")
    if not qt_prerequisites_declared:
        fatal.append("canonical package does not declare Qt5 JSCore/GL/Weston prerequisites")
    if not effective_qt_configurations_ready:
        invalid = [
            "{} [{}]".format(
                variant["source"],
                ", ".join(variant["missing_effective_qt_symbols"]),
            )
            for variant in configuration_variants
            if variant["missing_effective_qt_symbols"]
        ]
        fatal.append(
            "effective BoardLab configs are missing Qt5 modules: {}".format(
                "; ".join(invalid)
            )
        )

    unknown_architectures = [
        configuration_variants[index]["source"]
        for index, value in enumerate(architectures)
        if value == "unknown"
    ]
    if unknown_architectures:
        warnings.append(
            "target architecture is unresolved in: {}".format(
                ", ".join(unknown_architectures)
            )
        )
    unknown_libcs = [
        configuration_variants[index]["source"]
        for index, value in enumerate(libcs)
        if value == "unknown"
    ]
    if unknown_libcs:
        warnings.append(
            "target libc is unresolved in: {}".format(", ".join(unknown_libcs))
        )
    non_weston_displays = [
        "{}={}".format(configuration_variants[index]["source"], value)
        for index, value in enumerate(displays)
        if value != "Wayland/Weston"
    ]
    if non_weston_displays:
        warnings.append(
            "BoardLab configs without confirmed Wayland/Weston: {}".format(
                ", ".join(non_weston_displays)
            )
        )
    unknown_inits = [
        configuration_variants[index]["source"]
        for index, value in enumerate(init_systems)
        if value == "unknown"
    ]
    if unknown_inits:
        warnings.append(
            "target init system is unresolved in: {}".format(
                ", ".join(unknown_inits)
            )
        )
    unsupported_inits = [
        "{}={}".format(configuration_variants[index]["source"], value)
        for index, value in enumerate(init_systems)
        if value not in (
            "unknown", "busybox/SysV", "busybox/SysV (default)", "SysV"
        )
    ]
    if unsupported_inits:
        warnings.append(
            "canonical BoardLab package provides SysV autostart but configs use: {}".format(
                ", ".join(unsupported_inits)
            )
        )
    if not gst_ready:
        missing_gst = []
        if not gst_symbol:
            missing_gst.append("Buildroot appsink symbol")
        if not gst_selected:
            missing_gst.append("canonical BoardLab appsink select")
        if not gst_required:
            missing_gst.append("canonical BOARDLAB_REQUIRE_GSTREAMER_APPSINK=ON")
        if not gst_dependencies_declared:
            missing_gst.append("canonical GStreamer package dependencies")
        if not gst_packages_available:
            missing_gst.append("SDK GStreamer packages")
        if not convert_ready:
            missing_gst.append("videoconvert/videoscale plugin layout")
        if not good_symbols_available:
            missing_gst.append("SDK V4L2/JPEG/multifile plugin symbols")
        if not good_symbols_selected:
            missing_gst.append("canonical V4L2/JPEG/multifile selects")
        fatal.append("GStreamer appsink contract missing: " + ", ".join(missing_gst))

    if not storage_tools_ready:
        missing_storage_tools = []
        if not storage_tool_symbols:
            missing_storage_tools.append("SDK util-linux basic-binaries symbols")
        if not storage_tools_selected:
            missing_storage_tools.append("canonical util-linux basic-binaries selects")
        if not storage_tools_dependency:
            missing_storage_tools.append("canonical util-linux package dependency")
        fatal.append(
            "Storage discovery contract missing: " + ", ".join(missing_storage_tools)
        )

    if npu_enabled:
        invalid_npu_architectures = [
            "{}={}".format(configuration_variants[index]["source"], architectures[index])
            for index, enabled in enumerate(npu_enabled_by_config)
            if enabled and architectures[index] not in ("aarch64", "arm64", "ARM64")
        ]
        if invalid_npu_architectures:
            warnings.append(
                "NPU is enabled for non-aarch64 configs: {}".format(
                    ", ".join(invalid_npu_architectures)
                )
            )
        missing_npu = []
        if not rknn["available"]:
            missing_npu.append("rknpu2 package")
        if not opencv["available"]:
            missing_npu.append("opencv4 package")
        if not npu_dependencies:
            missing_npu.append("canonical BoardLab rknpu2/opencv4 dependencies")
        if missing_npu:
            fatal.append("NPU configuration is enabled but missing " + ", ".join(missing_npu))

    if fatal:
        status = "unsupported"
    elif warnings:
        status = "degraded"
    else:
        status = "supported"
    reasons = fatal + warnings
    if not reasons:
        reasons = ["canonical source and package contract satisfy this SDK"]

    deployment_reasons = []
    if application_comparison["drift"]:
        deployment_reasons.append(
            "SDK app copy differs: {} changed, {} missing, {} extra files".format(
                len(application_comparison["changed_files"]),
                len(application_comparison["missing_files"]),
                len(application_comparison["extra_files"]),
            )
        )
    if package_comparison["drift"]:
        deployment_reasons.append(
            "SDK package copy differs: {} changed, {} missing canonical files".format(
                len(package_comparison["changed_files"]),
                len(package_comparison["missing_files"]),
            )
        )
    deployment_drifted = application_comparison["drift"] or package_comparison["drift"]
    if not deployment_reasons:
        deployment_reasons = ["SDK-local application and package copies match canonical content"]

    if gst_ready:
        gst_status = "ready"
    elif gst_symbol:
        gst_status = "incomplete"
    else:
        gst_status = "missing"

    source_compatibility = {
        "status": status,
        "fatal_reasons": fatal,
        "degraded_reasons": warnings,
        "reasons": reasons,
    }
    deployment_drift = {
        "status": "drifted" if deployment_drifted else "clean",
        "drift": deployment_drifted,
        "application": application_comparison,
        "package": package_comparison,
        "reasons": deployment_reasons,
    }
    return {
        "sdk": os.path.relpath(sdk_root, linux_root).replace(os.sep, "/"),
        "path": sdk_root,
        "kernel": kernel,
        "buildroot": buildroot_release,
        "cmake": {
            "available": available_cmake,
            "required": application["cmake"],
            "compatible": cmake_compatible,
        },
        "qt": {
            "available": available_qt,
            "required": application["qt5"],
            "compatible": qt_compatible,
            "required_kconfig_symbols": list(required_qt_symbols),
            "kconfig_symbols_available": qt_symbol_availability,
            "selected_by_boardlab": qt_symbols_selected,
            "prerequisites_declared_by_boardlab": qt_prerequisites_declared,
            "package_dependencies": qt_dependencies_declared,
        },
        "target": {"architecture": architecture, "libc": libc},
        "runtime": {
            "display": display,
            "init": init_system,
            "storage_tools": {
                "status": "ready" if storage_tools_ready else "missing",
                "symbols_available": storage_tool_symbols,
                "selected_by_boardlab": storage_tools_selected,
                "package_dependency": storage_tools_dependency,
            },
        },
        "configuration": {
            "kind": config["kind"],
            "sources": config["sources"],
            "variants": configuration_variants,
        },
        "gstreamer_appsink": {
            "status": gst_status,
            "contract_source": "canonical-package",
            "plugin_symbol_available": gst_symbol,
            "selected_by_boardlab": gst_selected,
            "required_by_boardlab": gst_required,
            "package_dependencies": gst_dependencies,
            "dependencies_declared": gst_dependencies_declared,
            "sdk_packages_available": gst_packages_available,
            "video_convert_layout": convert_layout,
            "video_convert_ready": convert_ready,
            "runtime_plugin_symbols_available": good_symbols_available,
            "runtime_plugins_selected": good_symbols_selected,
        },
        "npu": {
            "enabled": npu_enabled,
            "enabled_configs": [
                configuration_variants[index]["source"]
                for index, enabled in enumerate(npu_enabled_by_config)
                if enabled
            ],
            "provider": rknn_provider(canonical["package_makefile"]),
            "contract_source": "canonical-package",
            "dependencies_declared": npu_dependencies,
            "rknn": rknn,
            "opencv": opencv,
        },
        "source_compatibility": source_compatibility,
        "deployment_drift": deployment_drift,
        "application": application_comparison,
        "package": package_comparison,
        "status": status,
        "fatal_reasons": fatal,
        "degraded_reasons": warnings,
        "reasons": reasons,
    }


def table_cell(value):
    return str(value).replace("\n", " ").replace("|", "/")


def table_rows(results):
    rows = []
    for item in results:
        npu = item["npu"]
        npu_cell = "{}; {}/{}".format(
            npu["provider"], short_source(npu["rknn"]), short_source(npu["opencv"])
        )
        deployment = item["deployment_drift"]
        application = deployment["application"]
        application_cell = "{}/{} {}".format(
            application["canonical_hash"][:10],
            application["sdk_hash"][:10],
            "drift" if application["drift"] else "match",
        )
        package = deployment["package"]
        package_cell = "{}/{} {}".format(
            package["canonical_hash"][:10],
            package["sdk_hash"][:10],
            "drift" if package["drift"] else "match",
        )
        rows.append(
            [
                item["sdk"],
                item["kernel"],
                item["buildroot"],
                "{} (need {})".format(item["cmake"]["available"], item["cmake"]["required"]),
                "{} (need {})".format(item["qt"]["available"], item["qt"]["required"]),
                "{}/{}".format(item["target"]["architecture"], item["target"]["libc"]),
                "{}/{}".format(item["runtime"]["display"], item["runtime"]["init"]),
                item["gstreamer_appsink"]["status"],
                npu_cell,
                application_cell,
                package_cell,
                deployment["status"] + ": " + "; ".join(deployment["reasons"]),
                item["source_compatibility"]["status"],
                "; ".join(item["source_compatibility"]["reasons"]),
            ]
        )
    return rows


def print_table(results, canonical, linux_root):
    headers = [
        "SDK",
        "KERNEL",
        "BUILDROOT",
        "CMAKE",
        "QT",
        "ARCH/LIBC",
        "DISPLAY/INIT",
        "APPSINK",
        "RKNN/OPENCV",
        "APP C/S SHA",
        "PACKAGE C/S SHA",
        "DEPLOYMENT DRIFT",
        "SOURCE STATUS",
        "SOURCE REASONS",
    ]
    rows = table_rows(results)
    widths = []
    for index, header in enumerate(headers):
        widths.append(max([len(header)] + [len(table_cell(row[index])) for row in rows]))
    separator = "-+-".join("-" * width for width in widths)
    print("Linux root: {}".format(linux_root))
    print(
        "Canonical source: {} ({})".format(
            canonical["project_root"], canonical["source_hash"]
        )
    )
    print(
        "Canonical package: {} ({})".format(
            canonical["package_path"], canonical["package_hash"]
        )
    )
    print("SDKs: {}".format(len(results)))
    print()
    print(" | ".join(header.ljust(widths[index]) for index, header in enumerate(headers)))
    print(separator)
    for row in rows:
        print(
            " | ".join(
                table_cell(value).ljust(widths[index])
                for index, value in enumerate(row)
            )
        )
    counts = collections.Counter(
        item["source_compatibility"]["status"] for item in results
    )
    drifted = sum(1 for item in results if item["deployment_drift"]["drift"])
    variant_counts = collections.Counter(
        variant["status"]
        for item in results
        for variant in item["configuration"]["variants"]
    )
    print()
    print(
        "Source compatibility: supported={} degraded={} unsupported={}".format(
            counts.get("supported", 0),
            counts.get("degraded", 0),
            counts.get("unsupported", 0),
        )
    )
    print("Deployment drift: clean={} drifted={}".format(len(results) - drifted, drifted))
    print(
        "Configuration variants: supported={} declared-compatible={} ineligible={} unsupported={}".format(
            variant_counts.get("supported", 0),
            variant_counts.get("declared-compatible", 0),
            variant_counts.get("ineligible", 0),
            variant_counts.get("unsupported", 0),
        )
    )


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description=(
            "Read-only scan of SDKs containing "
            "buildroot/package/rockchip/boardlab/boardlab.mk"
        )
    )
    parser.add_argument(
        "--linux-root",
        default=DEFAULT_LINUX_ROOT,
        help="parent directory to scan (default: %(default)s)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        dest="json_output",
        help="emit machine-readable JSON instead of the table",
    )
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    linux_root = os.path.abspath(os.path.expanduser(arguments.linux_root))
    if not os.path.isdir(linux_root):
        print("error: Linux root is not a directory: {}".format(linux_root), file=sys.stderr)
        return 2

    project_root = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    canonical_package = os.path.join(
        project_root, "packaging", "buildroot", PACKAGE_RELATIVE
    )
    canonical_files = all_files(canonical_package)
    if not canonical_files:
        print(
            "error: canonical BoardLab package is missing or empty: {}".format(
                canonical_package
            ),
            file=sys.stderr,
        )
        return 2
    canonical_hash = manifest_hash(canonical_package, canonical_files)
    canonical_makefile = os.path.join(canonical_package, "boardlab.mk")
    canonical_make = read_text(canonical_makefile)
    required_payloads = required_package_payloads(canonical_make)
    missing_required_payloads = [
        relative
        for relative in required_payloads
        if not os.path.isfile(os.path.join(canonical_package, relative))
    ]
    source_manifest = tree_manifest(
        project_root, SOURCE_EXCLUDED_ROOT_ENTRIES, SOURCE_EXCLUDED_ROOT_PREFIXES
    )
    source_hash = manifest_map_hash(source_manifest)
    canonical = {
        "project_root": project_root,
        "source_manifest": source_manifest,
        "source_hash": source_hash,
        "application_requirements": application_requirements(project_root),
        "package_path": canonical_package,
        "package_files": canonical_files,
        "package_hash": canonical_hash,
        "package_makefile": canonical_makefile,
        "package_make": canonical_make,
        "package_config": read_text(os.path.join(canonical_package, "Config.in")),
        "required_package_payloads": required_payloads,
        "missing_required_package_payloads": missing_required_payloads,
    }

    sdk_roots, scan_errors = walk_sdk_roots(linux_root)
    results = [
        analyze_sdk(sdk_root, linux_root, canonical)
        for sdk_root in sdk_roots
    ]
    counts = collections.Counter(
        item["source_compatibility"]["status"] for item in results
    )
    drifted = sum(1 for item in results if item["deployment_drift"]["drift"])
    variant_counts = collections.Counter(
        variant["status"]
        for item in results
        for variant in item["configuration"]["variants"]
    )
    if arguments.json_output:
        payload = {
            "schema_version": 3,
            "linux_root": linux_root,
            "canonical_source": {
                "path": project_root,
                "hash": source_hash,
                "file_count": len(source_manifest),
                "hash_scope": "distributable-source-files",
                "excluded_root_entries": sorted(SOURCE_EXCLUDED_ROOT_ENTRIES),
                "excluded_root_prefixes": list(SOURCE_EXCLUDED_ROOT_PREFIXES),
            },
            "canonical_package": {
                "path": canonical_package,
                "hash": canonical_hash,
                "files": canonical_files,
                "required_payloads": required_payloads,
                "missing_required_payloads": missing_required_payloads,
                "hash_scope": "canonical-files",
            },
            "sdk_count": len(results),
            "summary": {
                "supported": counts.get("supported", 0),
                "degraded": counts.get("degraded", 0),
                "unsupported": counts.get("unsupported", 0),
                "deployment_clean": len(results) - drifted,
                "deployment_drifted": drifted,
                "config_resolved_supported": variant_counts.get("supported", 0),
                "config_declared_compatible": variant_counts.get(
                    "declared-compatible", 0
                ),
                "config_ineligible": variant_counts.get("ineligible", 0),
                "config_unsupported": variant_counts.get("unsupported", 0),
            },
            "scan_errors": scan_errors,
            "sdks": results,
        }
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print_table(results, canonical, linux_root)
        for error in scan_errors:
            print("warning: scan skipped {}".format(error), file=sys.stderr)

    if not results:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
