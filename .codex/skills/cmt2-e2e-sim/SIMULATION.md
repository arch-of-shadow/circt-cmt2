# CMT2 Simulation Workflow

## Supported Simulators

- **Icarus Verilog (iverilog)** - Open source, good for basic testing
- **Verilator** - Fast cycle-accurate simulation
- **Commercial** - VCS, ModelSim, Xcelium (if available)

## Simulation Workspace Structure

```
sim_workspace/
├── rtl/
│   ├── design.sv          # Generated SystemVerilog
│   └── primitives.sv      # FIRRTL primitives
├── tb/
│   └── testbench.sv       # Testbench
├── sim/
│   ├── Makefile            # Build rules
│   └── sim.vcd             # Waveform output
└── logs/
    └── sim.log             # Simulation log
```

## Creating a Simulation Workspace

### Manual Steps

1. **Generate SystemVerilog**:
```bash
build/bin/circt-opt input.mlir -cmt2-to-firrtl | \
build/bin/firtool --format=mlir -o rtl/design.sv
```

2. **Create testbench** (see TESTBENCH.md)

3. **Compile with iverilog**:
```bash
iverilog -g2012 -o sim.out rtl/*.sv tb/*.sv
```

4. **Run simulation**:
```bash
vvp sim.out
```

### Using Scripts

```bash
# Create complete workspace
.claude/skills/cmt2-e2e-sim/scripts/create-sim-workspace.sh input.mlir workspace/

# Run simulation
.claude/skills/cmt2-e2e-sim/scripts/run-sim.sh workspace/
```

## Viewing Waveforms

Generate VCD:
```bash
vvp sim.out +vcd=sim.vcd
```

View with GTKWave:
```bash
gtkwave sim.vcd
```

## Simulation Checklist

- [ ] SystemVerilog generated successfully
- [ ] Testbench instantiates DUT correctly
- [ ] Clock and reset signals connected
- [ ] Stimulus provided for inputs
- [ ] Expected outputs checked
- [ ] Simulation completes without errors
- [ ] Results match expected values

## Common Issues

### X or Z values in simulation
- Check reset initialization
- Verify all inputs are driven

### Timing violations
- Check clock period matches design constraints
- Verify setup/hold requirements

### Mismatched widths
- Check port widths in testbench match generated RTL
