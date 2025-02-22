# QEMU Usage Documentation

## How to Use the Scripts

### Linux
- **run_qemu.sh**: Launches QEMU normally. Example usage:
  ```sh
  ./run_qemu.sh
  ```

- **run_qemu_debug.sh**: Launches QEMU in debug mode with instruction-level tracing. Example usage:
  ```sh
  ./run_qemu_debug.sh
  ```

### Windows
- **run_qemu.bat**: Launches QEMU normally. Run it from Command Prompt.
- **run_qemu_debug.bat**: Launches QEMU in debug mode with instruction-level tracing.

## QEMU Options Explanation

- `-machine virt`: Specifies the virtual board (virt board).
- `-kernel tsukumo-core.elf`: Loads the kernel image (tsukumo-core.elf).
- `-nographic`: Disables the GUI and uses the serial console.
- `-d in_asm`: Outputs instruction-level assembly logs (for debugging).
