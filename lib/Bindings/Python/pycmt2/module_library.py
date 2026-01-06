#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Module Library for PyCMT2.

This module provides access to the CMT2 ModuleLibrary, which contains
FIRRTL module implementations for external modules like Reg, Wire, Memory.

The ModuleLibrary:
1. Parses manifest.yaml to get module info
2. Calls build.sh scripts to generate FIRRTL from Chisel
3. Parses the generated FIRRTL and includes it in the circuit

This mirrors the C++ ModuleLibrary in lib/Dialect/Cmt2/ECMT2/ModuleLibrary.cpp
"""

from __future__ import annotations

import os
import subprocess
import re
from pathlib import Path
from typing import TYPE_CHECKING, Optional
import yaml

if TYPE_CHECKING:
    from circt.ir import Module as MLIRModule


class ModuleLibrary:
    """Singleton class for accessing the CMT2 Module Library.

    The ModuleLibrary manages parametric FIRRTL modules (Reg, Wire, Memory)
    that are built from Chisel sources on demand.
    """

    _instance: Optional[ModuleLibrary] = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True
        self._library_base_path: Optional[Path] = None
        self._modules: dict[str, dict] = {}
        self._cache: dict[str, str] = {}  # Cache of generated FIRRTL
        self._included_modules: set[str] = set()  # Track modules already in circuit

    @classmethod
    def get_instance(cls) -> ModuleLibrary:
        """Get the singleton instance."""
        return cls()

    def load_manifest(self, manifest_path: str | Path) -> bool:
        """Load the module library manifest.

        Args:
            manifest_path: Path to manifest.yaml

        Returns:
            True if successful, False otherwise.
        """
        manifest_path = Path(manifest_path)
        if not manifest_path.exists():
            return False

        self._library_base_path = manifest_path.parent

        try:
            with open(manifest_path) as f:
                manifest = yaml.safe_load(f)
        except Exception as e:
            print(f"Failed to parse manifest: {e}")
            return False

        # Parse modules
        for module_info in manifest.get("modules", []):
            name = module_info.get("name")
            if not name:
                continue

            self._modules[name] = {
                "name": name,
                "type": module_info.get("type", "chisel"),
                "path": module_info.get("path", ""),
                "description": module_info.get("description", ""),
                "parameters": module_info.get("parameters", []),
                "build": module_info.get("build", {}),
                "methods": module_info.get("methods", []),
                "values": module_info.get("values", []),
                "conflict_matrix": module_info.get("conflict_matrix", []),
            }

        return True

    def _find_project_root(self) -> Optional[Path]:
        """Find the circt-cmt2 project root directory.

        Looks for markers that indicate the project root:
        - CLAUDE.md file
        - .git directory with circt-cmt2 in path
        - CMakeLists.txt with CIRCT

        Returns:
            Path to project root or None if not found.
        """
        # Start from current working directory and check upwards
        cwd = Path.cwd().resolve()

        # Also check relative to this source file
        this_file = Path(__file__).resolve()
        # This file is at lib/Bindings/Python/pycmt2/module_library.py
        # Project root is 4 levels up
        source_root = this_file.parent.parent.parent.parent.parent

        candidates = [cwd, source_root]

        # Check if cwd looks like build/ directory
        if cwd.name == "build" and (cwd.parent / "CLAUDE.md").exists():
            candidates.insert(0, cwd.parent)

        for candidate in candidates:
            # Check for CLAUDE.md (our project marker)
            if (candidate / "CLAUDE.md").exists():
                return candidate
            # Check for lib/Dialect/Cmt2 structure
            if (candidate / "lib" / "Dialect" / "Cmt2" / "ModuleLibrary" / "manifest.yaml").exists():
                return candidate

        return None

    def find_library_path(self) -> Optional[Path]:
        """Find the ModuleLibrary path.

        Searches in:
        1. Environment variable CMT2_MODULE_LIBRARY_PATH
        2. Project root / lib/Dialect/Cmt2/ModuleLibrary
        3. Common relative paths from cwd

        Returns:
            Path to manifest.yaml if found, None otherwise.
        """
        # Check environment variable first
        env_path = os.environ.get("CMT2_MODULE_LIBRARY_PATH")
        if env_path:
            manifest = Path(env_path) / "manifest.yaml"
            if manifest.exists():
                return manifest

        # Find project root and use canonical path
        project_root = self._find_project_root()
        if project_root:
            manifest = project_root / "lib" / "Dialect" / "Cmt2" / "ModuleLibrary" / "manifest.yaml"
            if manifest.exists():
                return manifest

        # Fallback: search relative paths
        search_paths = [
            Path("../lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml"),
            Path("lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml"),
        ]

        for path in search_paths:
            if path.exists():
                return path

        return None

    def ensure_loaded(self) -> bool:
        """Ensure the manifest is loaded.

        Returns:
            True if loaded, False if not found.
        """
        if self._library_base_path is not None:
            return True

        manifest_path = self.find_library_path()
        if manifest_path is None:
            return False

        return self.load_manifest(manifest_path)

    def has_module(self, name: str) -> bool:
        """Check if a module exists in the library."""
        self.ensure_loaded()
        return name in self._modules

    def get_module_info(self, name: str) -> Optional[dict]:
        """Get module info from the library."""
        self.ensure_loaded()
        return self._modules.get(name)

    def _substitute_params(self, pattern: str, params: dict[str, int]) -> str:
        """Substitute ${param} placeholders with values."""
        result = pattern
        for key, value in params.items():
            result = result.replace(f"${{{key}}}", str(value))
        return result

    def get_actual_module_name(self, name: str, params: dict[str, int]) -> Optional[str]:
        """Get the actual FIRRTL module name with parameters substituted.

        Args:
            name: Library module name (e.g., "FIRRTLReg")
            params: Parameters (e.g., {"width": 32, "init": 0})

        Returns:
            Actual module name (e.g., "Reg_width32_init0") or None if not found.
        """
        info = self.get_module_info(name)
        if not info:
            return None

        build_info = info.get("build", {})
        output_pattern = build_info.get("output_pattern", "")
        if not output_pattern:
            return name

        # Merge with defaults
        complete_params = dict(params)
        for param_info in info.get("parameters", []):
            param_name = param_info.get("name")
            if param_name and param_name not in complete_params:
                if "default" in param_info:
                    complete_params[param_name] = param_info["default"]

        # Substitute and extract module name from filename
        output_file = self._substitute_params(output_pattern, complete_params)
        # Remove .mlir extension
        if output_file.endswith(".mlir"):
            output_file = output_file[:-5]
        return output_file

    def build_module(self, name: str, params: dict[str, int]) -> Optional[str]:
        """Build a module and return the generated FIRRTL as a string.

        Args:
            name: Library module name (e.g., "FIRRTLReg")
            params: Parameters (e.g., {"width": 32, "init": 0})

        Returns:
            FIRRTL MLIR string or None if build failed.
        """
        if not self.ensure_loaded():
            print("ModuleLibrary not loaded")
            return None

        info = self.get_module_info(name)
        if not info:
            print(f"Module {name} not found in library")
            return None

        # Check cache
        actual_name = self.get_actual_module_name(name, params)
        if actual_name and actual_name in self._cache:
            return self._cache[actual_name]

        build_info = info.get("build", {})
        command = build_info.get("command", "")
        output_pattern = build_info.get("output_pattern", "")

        if not command or not output_pattern:
            print(f"No build info for module {name}")
            return None

        # Build directory
        build_dir = self._library_base_path / info.get("path", "")

        # Merge params with defaults
        complete_params = dict(params)
        for param_info in info.get("parameters", []):
            param_name = param_info.get("name")
            if param_name and param_name not in complete_params:
                if "default" in param_info:
                    complete_params[param_name] = param_info["default"]

        # Build command with parameters
        cmd = str(build_dir / command)
        args = [cmd]
        for param_name, value in complete_params.items():
            args.append(f"{param_name}={value}")

        # Run build script
        try:
            result = subprocess.run(
                args,
                cwd=str(build_dir),
                capture_output=True,
                text=True,
                timeout=60,
            )
            if result.returncode != 0:
                print(f"Build failed for {name}: {result.stderr}")
                return None
        except subprocess.TimeoutExpired:
            print(f"Build timed out for {name}")
            return None
        except Exception as e:
            print(f"Build error for {name}: {e}")
            return None

        # Read output file
        output_file = self._substitute_params(output_pattern, complete_params)
        output_path = build_dir / output_file

        try:
            with open(output_path) as f:
                firrtl_content = f.read()
        except Exception as e:
            print(f"Failed to read build output {output_path}: {e}")
            return None

        # Cache result
        if actual_name:
            self._cache[actual_name] = firrtl_content

        return firrtl_content

    def include_module_in_circuit(
        self,
        name: str,
        params: dict[str, int],
        mlir_module=None,  # Not used during construction
    ) -> Optional[str]:
        """Build a module and return its actual name.

        This method builds the FIRRTL module from Chisel sources and caches
        the result. The cached FIRRTL content is later inserted into the
        circuit by `insert_cached_firrtl_modules()` before lowering.

        Args:
            name: Library module name (e.g., "FIRRTLReg")
            params: Parameters (e.g., {"width": 32, "init": 0})
            mlir_module: Unused, kept for API compatibility

        Returns:
            The actual FIRRTL module name (e.g., "Reg_width32_init0"), or None if failed.
        """
        actual_name = self.get_actual_module_name(name, params)
        if not actual_name:
            return None

        # Check if already built
        if actual_name in self._included_modules:
            return actual_name

        # Build the module (this caches the generated FIRRTL)
        firrtl_content = self.build_module(name, params)
        if not firrtl_content:
            # Return the expected name anyway - the C++ ModuleLibrary will
            # build it during lowering if not cached
            return actual_name

        self._included_modules.add(actual_name)
        return actual_name

    def get_cached_firrtl(self, module_name: str) -> Optional[str]:
        """Get cached FIRRTL content for a module.

        Args:
            module_name: The actual module name (e.g., "Reg_width32_init0")

        Returns:
            The FIRRTL MLIR string or None if not cached.
        """
        return self._cache.get(module_name)

    def insert_cached_firrtl_modules(self, mlir_module) -> bool:
        """Insert all cached FIRRTL modules into the firrtl.circuit.

        This should be called AFTER lower-cmt2-to-firrtl pass, which creates
        the firrtl.circuit. The cached modules are inserted into the existing
        firrtl.circuit so the lowering pipeline can find them.

        Args:
            mlir_module: The MLIR module containing a firrtl.circuit.

        Returns:
            True if successful, False if any errors occurred.
        """
        if not self._cache:
            return True  # Nothing to insert

        from circt.ir import InsertionPoint, Module, Location

        ctx = mlir_module.context

        # Find the firrtl.circuit in the target module
        target_circuit = None
        for op in mlir_module.body.operations:
            if op.operation.name == "firrtl.circuit":
                target_circuit = op
                break

        if not target_circuit:
            print("No firrtl.circuit found in target module")
            return False

        # Get the body block of the target firrtl.circuit
        # firrtl.circuit has one region with one block
        target_block = target_circuit.regions[0].blocks[0]

        success = True
        inserted = set()

        for module_name, firrtl_content in self._cache.items():
            if module_name in inserted:
                continue

            try:
                # Parse the cached FIRRTL
                parsed = Module.parse(firrtl_content, ctx)

                # Find the firrtl.circuit in the parsed module
                source_circuit = None
                for op in parsed.body.operations:
                    if op.operation.name == "firrtl.circuit":
                        source_circuit = op
                        break

                if not source_circuit:
                    print(f"No firrtl.circuit found in cached FIRRTL for {module_name}")
                    continue

                # Get source circuit's body block
                source_block = source_circuit.regions[0].blocks[0]

                # Clone the firrtl.module operations into the target circuit
                with InsertionPoint.at_block_begin(target_block):
                    for inner_op in source_block:
                        if inner_op.operation.name == "firrtl.module":
                            inner_op.operation.clone()
                            inserted.add(module_name)

            except Exception as e:
                print(f"Failed to insert FIRRTL module {module_name}: {e}")
                success = False

        return success

    def get_verilog_for_modules(self) -> dict[str, str]:
        """Convert cached FIRRTL modules to Verilog.

        Returns:
            Dict mapping module name to Verilog content.
        """
        if not self._cache:
            return {}

        from circt.ir import Context, Module as MLIRModule
        from circt.passmanager import PassManager
        from circt.dialects import firrtl
        from circt import export_verilog
        import io

        result = {}

        for module_name, firrtl_content in self._cache.items():
            try:
                # Create a fresh context for each module
                ctx = Context()
                firrtl.register_dialect(ctx)

                # Parse the FIRRTL
                mlir_module = MLIRModule.parse(firrtl_content, ctx)

                # Run FIRRTL lowering passes
                # Include memory lowering passes for firrtl.mem operations
                pm = PassManager.parse(
                    "builtin.module("
                    "firrtl.circuit("
                    "firrtl-infer-resets,"
                    "firrtl-lower-types"
                    "),"
                    "any(any(firrtl-expand-whens)),"
                    "firrtl.circuit("
                    "any(firrtl-infer-rw),"
                    "firrtl-lower-memory"
                    "),"
                    "lower-firrtl-to-hw,"
                    "lower-seq-to-sv,"
                    "lower-seq-firmem,"
                    "hw-memory-sim"
                    ")",
                    context=ctx
                )
                pm.run(mlir_module.operation)

                # Export to Verilog
                output = io.StringIO()
                export_verilog(mlir_module, output)
                result[module_name] = output.getvalue()

            except Exception as e:
                print(f"Failed to convert {module_name} to Verilog: {e}")

        return result

    def reset(self):
        """Reset the library state (for testing)."""
        self._included_modules.clear()


# Convenience function
def get_module_library() -> ModuleLibrary:
    """Get the global ModuleLibrary instance."""
    return ModuleLibrary.get_instance()
