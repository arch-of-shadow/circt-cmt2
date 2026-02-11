#!/usr/bin/env python3
"""Generate API Reference documentation from PyCMT2 docstrings.

This script extracts docstrings from pycmt2 modules and generates
a markdown API reference document.

Usage:
    cd build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 \
        ../scripts/generate_pycmt2_api_docs.py > ../docs/Cmt2/reference/API-Reference.md
"""

import sys
import inspect
import importlib
from pathlib import Path

# Add pycmt2 to path
sys.path.insert(0, str(Path(__file__).parent.parent / "build" / "tools" / "circt" / "python_packages" / "circt_core"))

def get_signature(obj):
    """Get function/method signature."""
    try:
        sig = inspect.signature(obj)
        return str(sig)
    except (ValueError, TypeError):
        return "()"

def format_docstring(doc):
    """Format docstring for markdown."""
    if not doc:
        return "*No documentation available.*"
    # Clean up indentation
    lines = doc.strip().split('\n')
    if len(lines) > 1:
        # Find minimum indentation
        min_indent = float('inf')
        for line in lines[1:]:
            if line.strip():
                indent = len(line) - len(line.lstrip())
                min_indent = min(min_indent, indent)
        if min_indent < float('inf'):
            lines = [lines[0]] + [line[min_indent:] if len(line) > min_indent else line for line in lines[1:]]
    return '\n'.join(lines)

def document_class(cls, module_name):
    """Generate documentation for a class."""
    output = []
    output.append(f"### `{cls.__name__}`\n")

    doc = format_docstring(cls.__doc__)
    output.append(f"{doc}\n")

    # Get methods
    methods = []
    for name, method in inspect.getmembers(cls, predicate=lambda x: inspect.isfunction(x) or inspect.ismethod(x)):
        if not name.startswith('_') or name in ('__init__', '__enter__', '__exit__'):
            methods.append((name, method))

    if methods:
        output.append("#### Methods\n")
        for name, method in sorted(methods):
            sig = get_signature(method)
            output.append(f"**`{name}{sig}`**\n")
            doc = format_docstring(method.__doc__)
            if doc != "*No documentation available.*":
                output.append(f"> {doc.split(chr(10))[0]}\n")
            output.append("")

    return '\n'.join(output)

def document_function(func, module_name):
    """Generate documentation for a function."""
    sig = get_signature(func)
    doc = format_docstring(func.__doc__)
    return f"### `{func.__name__}{sig}`\n\n{doc}\n"

def document_module(module, name):
    """Generate documentation for a module."""
    output = []
    output.append(f"## {name}\n")

    if module.__doc__:
        output.append(format_docstring(module.__doc__))
        output.append("")

    # Classes
    classes = []
    for item_name, item in inspect.getmembers(module, inspect.isclass):
        if item.__module__ == module.__name__ and not item_name.startswith('_'):
            classes.append((item_name, item))

    if classes:
        for cls_name, cls in sorted(classes):
            output.append(document_class(cls, name))

    # Functions
    functions = []
    for item_name, item in inspect.getmembers(module, inspect.isfunction):
        if item.__module__ == module.__name__ and not item_name.startswith('_'):
            functions.append((item_name, item))

    if functions:
        output.append("### Functions\n")
        for func_name, func in sorted(functions):
            output.append(document_function(func, name))

    return '\n'.join(output)

def main():
    print("# PyCMT2 API Reference\n")
    print("**Auto-generated from docstrings**\n")
    print("---\n")

    # List of modules to document
    modules_to_doc = [
        ("Circuit", "pycmt2.circuit"),
        ("Module Builder", "pycmt2.module"),
        ("Types", "pycmt2.types"),
        ("STL Components", "pycmt2.stl"),
        ("Dataflow Builders", "pycmt2.dataflow_builders"),
        ("Pipeline Builders", "pycmt2.pipeline_builders"),
        ("Timing", "pycmt2.timing"),
        ("Simulation", "pycmt2.simulation"),
        ("Testbench", "pycmt2.testbench"),
        ("External Module", "pycmt2.external_module"),
        ("Diagnostics", "pycmt2.diagnostics"),
    ]

    # Table of contents
    print("## Table of Contents\n")
    for title, mod_name in modules_to_doc:
        anchor = title.lower().replace(' ', '-')
        print(f"- [{title}](#{anchor})")
    print("")
    print("---\n")

    # Import and document each module
    for title, mod_name in modules_to_doc:
        try:
            # Import from circt.pycmt2
            full_name = f"circt.{mod_name}"
            module = importlib.import_module(full_name)
            print(document_module(module, title))
            print("---\n")
        except ImportError as e:
            print(f"## {title}\n")
            print(f"*Module `{mod_name}` could not be imported: {e}*\n")
            print("---\n")
        except Exception as e:
            print(f"## {title}\n")
            print(f"*Error documenting `{mod_name}`: {e}*\n")
            print("---\n")

if __name__ == "__main__":
    main()
