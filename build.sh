i686-elf-as boot.s -o boot.o
i686-elf-gcc -ffreestanding -c kernel.c -o kernel.o
i686-elf-gcc -T linker.ld -o myos.bin -ffreestanding -nostdlib boot.o kernel.o -lgcc
qemu-system-i386 -kernel myos.bin -display cocoa,zoom-to-fit=on
