# FIRRTLReg - Parametric Register Module

This directory contains a parametric register module implemented in Chisel for the Cmt2 module library.

## Features

- **Parametric Width**: Configurable bit width (default: 32 bits)
- **Ready-Enable Protocol**: Standard ready-enable interface for read and write operations
- **Always Ready**: Both read and write operations are always ready (no blocking)
- **Conflict Matrix**: Read must execute before write in the same cycle

## Files

- `FIRRTLReg.scala`: Chisel source code for the register module
- `build.sbt`: SBT build configuration for Chisel compilation
- `build.sh`: Build script that invokes Chisel and converts to MLIR

## Interface

```scala
class FIRRTLReg(width: Int = 32) extends Module {
  val io = IO(new Bundle {
    // Write interface (method)
    val write_enable = Input(Bool())
    val write_data = Input(UInt(width.W))
    val write_ready = Output(Bool())

    // Read interface (value)
    val read_ready = Output(Bool())
    val read_data = Output(UInt(width.W))
  })
}
```

## Ports

### Clock and Reset
- `clock`: Clock input (implicit in Chisel modules)
- `reset`: Reset input (implicit in Chisel modules)

### Write Method
- `write_enable`: Input enable signal (Bool)
- `write_data`: Input data to write (UInt<width>)
- `write_ready`: Output ready signal (always true)

### Read Value
- `read_ready`: Output ready signal (always true)
- `read_data`: Output data (current register value, UInt<width>)

## Behavior

- **Read**: Returns the current value of the register. Always ready, no side effects.
- **Write**: Updates the register when `write_enable` is true. Always ready.
- **Reset**: On reset, the register is initialized to 0.

## Conflict Matrix

The module specifies the following conflict relationship in the manifest:
- `read < write` (Sequential Before): Read must be scheduled before write in the same cycle

This ensures that when both read and write occur in the same cycle, the read operation sees the value before the write updates it.

## Build Process

The build script supports two modes:

### 1. Chisel Build (Production)
If SBT is available, the script:
1. Invokes Chisel to generate FIRRTL (.fir file)
2. Uses firtool to convert FIRRTL to MLIR
3. Produces `Reg_width<WIDTH>.mlir`

### 2. Direct MLIR Generation (Fallback)
If Chisel/SBT is not available, the script directly generates equivalent MLIR.

## Usage in ECMT2

```cpp
// Load library
auto &library = ModuleLibrary::getInstance();
library.loadManifest("lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");

// Create register with width=32
llvm::StringMap<int64_t> params;
params["width"] = 32;
auto *regMod = circuit.addExternalModule("reg", "FIRRTLReg", params);

// Bind interfaces
regMod->bindClock("clock")
      .bindReset("reset")
      .bindValue("read", "read_ready", {"read_data"})
      .bindMethod("write", "write_enable", "write_ready", {"write_data"}, {})
      .addSequenceBefore("read", "write");

// Use in a module
auto *reg = module->addInstance("myReg", regMod, {clk.getValue(), rst.getValue()});

// Call read value
auto readData = reg->callValue("read", builder);

// Call write method
reg->callMethod("write", {newData.getValue()}, builder);
```

## Development

To test the Chisel build locally:

```bash
cd lib/Dialect/Cmt2/ModuleLibrary/chisel/reg
sbt "runMain cmt2.lib.FIRRTLRegMain 32"  # Generates Reg_width32.fir
firtool --format=fir --ir-fir Reg_width32.fir  # Converts to MLIR
```

Or use the build script:

```bash
./build.sh 32  # Generates Reg_width32.mlir
```
