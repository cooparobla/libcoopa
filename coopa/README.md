# Coopa Module Root

Welcome to the `coopa` library module. This directory serves as the root container for the core modules powering the runtime execution, configuration, diagnostics, and scheduling engines.

## Module Structure

The package is divided into four main architectural layers:

1. **[`collections/`](./collections/)**
   - Wrapper for YAML maps and configuration deserializers/serializers using `fkYAML`.
2. **[`debug/`](./debug/)**
   - Thread-safe diagnostic logging systems, message buckets, and console log managers.
3. **[`util/`](./util/)**
   - General utilities including matrix math, relative file-path resolvers, string helpers, and thread-safe unique ID generation.
4. **[`job/`](./job/)**
   - Core concurrent multitasking engine featuring a work-stealing job queue, dedicated worker threads, and hazard-aware automatic job dependency graphs.

## Integration & Building

The `coopa` library is built with CMake as part of the `trav` executable or standalone library target.

To build, configure the target through the parent CMake structure:
```bash
cmake -S . -B build
cmake --build build
```
