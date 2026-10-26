.section .text
.code64
.global arch_load_gdt_and_tss
.type arch_load_gdt_and_tss, @function
/* void arch_load_gdt_and_tss(const struct descriptor_register *gdtr); */
arch_load_gdt_and_tss:
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movw $0x18, %ax
    ltr %ax
    ret
.size arch_load_gdt_and_tss, .-arch_load_gdt_and_tss
.section .note.GNU-stack,"",@progbits
