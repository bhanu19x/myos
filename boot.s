.set MAGIC, 0x1BADB002
.set FLAGS, 0
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
_start:
    mov $stack_top, %esp
    call kernel_main
    cli
1:  hlt
    jmp 1b

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $flush_done
flush_done:
    ret

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.global isr_timer
isr_timer:
    pusha
    call timer_handler
    popa
    iret

.global isr_keyboard
isr_keyboard:
    pusha
    call keyboard_handler
    popa
    iret
