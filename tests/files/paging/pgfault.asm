; This file is part of the dosbox-automation Project.
; License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
;
; Guest for test_guest_paging.py (ada-q50n). Build: nasm -f bin -o PGFAULT.COM
; Tail '1': the #PF handler maps the CR2 page, else it parks; 'z' unmaps page 0.
; Marker: "PGON" spinning, "PARK" parked, " EXC" other exception.

bits 16
org 0x100

start:
        xor ebp, ebp
        mov bp, cs
        shl ebp, 4

        mov si, 0x81
.skip:  lodsb
        cmp al, ' '
        je .skip
        cmp al, '1'
        jne .mode_set
        mov byte [mode], 1
.mode_set:
        lodsb
        or al, 0x20
        cmp al, 'z'
        jne .zero_set
        mov byte [unmap_zero], 1
.zero_set:
        cld
        mov eax, ebp
        add eax, area
        add eax, 0xFFF
        and eax, 0xFFFFF000
        mov [pd_lin], eax
        mov edi, eax
        sub edi, ebp

        push di
        xor eax, eax
        mov cx, 1024
        rep stosd
        pop di

        mov eax, [pd_lin]
        add eax, 0x1000
        or eax, 3
        mov [di], eax

        add di, 0x1000
        mov [pt_off], di
        mov eax, 3
        mov cx, 1024
.fill:  stosd
        add eax, 0x1000
        loop .fill

        mov di, [pt_off]
        mov dword [di + 0x200 * 4], 0
        mov dword [di + 0x101 * 4], 0
        cmp byte [unmap_zero], 1
        jne .tables_done
        mov dword [di], 0
.tables_done:

        mov eax, ebp
        add eax, gdt
        mov [gdtr + 2], eax

        mov di, idt
        mov cx, 32
        xor bx, bx
.idt:   mov eax, ebp
        cmp bx, 14
        jne .other
        add eax, pf_handler
        jmp .gate
.other: add eax, other_handler
.gate:  mov [di], ax
        mov word [di + 2], 0x08
        mov word [di + 4], 0x8E00
        shr eax, 16
        mov [di + 6], ax
        add di, 8
        inc bx
        loop .idt

        mov eax, ebp
        add eax, idt
        mov [idtr + 2], eax

        mov eax, ebp
        add eax, pm_entry
        mov [jmp_off], eax

        cli
        o32 lgdt [gdtr]
        o32 lidt [idtr]
        mov eax, [pd_lin]
        mov cr3, eax
        mov eax, cr0
        or eax, 0x80000001
        mov cr0, eax
        db 0x66, 0xEA
jmp_off:
        dd 0
        dw 0x08

bits 32
pm_entry:
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov fs, ax
        mov gs, ax
        lea esp, [ebp + stack_top]
        mov dword [ebp + marker], 0x4E4F4750
.spin:  jmp .spin

pf_handler:
        inc dword [ebp + pf_count]
        push eax
        mov eax, cr2
        mov [ebp + pf_cr2], eax
        cmp byte [ebp + mode], 1
        jne .park
        push ebx
        mov ebx, cr2
        and ebx, 0xFFFFF000
        mov eax, ebx
        shr eax, 10
        add eax, [ebp + pd_lin]
        or ebx, 3
        mov [eax + 0x1000], ebx
        pop ebx
        mov eax, cr3
        mov cr3, eax
        pop eax
        add esp, 4
        iretd
.park:  mov dword [ebp + marker], 0x4B524150
.parked:
        jmp .parked

other_handler:
        mov dword [ebp + marker], 0x43584520
.dead:  jmp .dead

align 4
gdt:    dq 0
        dw 0xFFFF, 0, 0x9A00, 0x00CF
        dw 0xFFFF, 0, 0x9200, 0x00CF
gdtr:   dw 23
        dd 0
idtr:   dw 32 * 8 - 1
        dd 0
marker:   dd 0
pf_count: dd 0
pf_cr2:   dd 0
pd_lin:   dd 0
pt_off:   dw 0
mode:     db 0
unmap_zero: db 0
align 8
idt:    times 32 * 8 db 0
align 16
stack:  times 1024 db 0
stack_top:
area:
