# tsukumo-core

This project aims to create a real-time operating system (RTOS) based on the uT-Kernel 3.0, targeting RISC-V architecture. The development is currently in the early stages, with the focus on porting basic C language code for processor handling.

## Current Status

- The project is in its early stages, and the code so far includes basic functionality for transferring processing to the RISC-V architecture.
- Target processor: **RISC-V**
- Emulator: **QEMU**
- The build environment is set up on **Windows**, using **Clang** as the compiler.

## Requirements

- **Windows** (Build environment)
- **Clang** (Compiler)
- **QEMU** (For running the RISC-V processor)

## Running on QEMU

1. After building the project, you can run the generated ELF file on QEMU with the following command:
   ```shell
   qemu-system-riscv32 -machine virt -bios none -kernel tsukumo-core.elf -nographic
   ```

   This will boot the RTOS in QEMU without a graphical interface.

## Next Steps

- Implement more features based on the uT-Kernel 3.0 design.
- Enhance the kernel functionality and ensure compatibility with RISC-V instructions.
- Expand the project to include inter-process communication, scheduling, and other essential RTOS components.

## Contributing

Feel free to fork the repository, make contributions, and submit pull requests. For any issues or suggestions, please create an issue in the issue tracker.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
