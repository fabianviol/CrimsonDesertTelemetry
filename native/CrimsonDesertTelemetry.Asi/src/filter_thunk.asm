; Hooked immediately after engine Dispatch: R12=filtered BufferD3D12 outer,
; RBX=engine command wrapper, original R15=paired counter outer, R13=owner.
; MinHook relocates the whole displaced instruction
; mov rbx,[rsp+50h] into CdtFilterTrampoline. No INT3/remove/rearm window exists.
; Save every GPR, RFLAGS and XMM0..15; dynamically align and provide shadow space.
; The helper is compiled for the normal MSVC x64/SSE2 ABI (no /arch:AVX).
EXTERN CdtCaptureFilter:PROC
EXTERN CdtFilterTrampoline:QWORD
.code
CdtFilterThunk PROC
    pushfq
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r15,rsp
    and rsp,-16
    sub rsp,130h
    mov [rsp+120h],r15
    movdqu [rsp+20h],xmm0
    movdqu [rsp+30h],xmm1
    movdqu [rsp+40h],xmm2
    movdqu [rsp+50h],xmm3
    movdqu [rsp+60h],xmm4
    movdqu [rsp+70h],xmm5
    movdqu [rsp+80h],xmm6
    movdqu [rsp+90h],xmm7
    movdqu [rsp+0A0h],xmm8
    movdqu [rsp+0B0h],xmm9
    movdqu [rsp+0C0h],xmm10
    movdqu [rsp+0D0h],xmm11
    movdqu [rsp+0E0h],xmm12
    movdqu [rsp+0F0h],xmm13
    movdqu [rsp+100h],xmm14
    movdqu [rsp+110h],xmm15
    mov rcx,r12
    mov rdx,rbx
    mov r8,[r15] ; original R15, saved before using R15 as the save-area pointer
    mov r9,r13
    call CdtCaptureFilter
    movdqu xmm0,[rsp+20h]
    movdqu xmm1,[rsp+30h]
    movdqu xmm2,[rsp+40h]
    movdqu xmm3,[rsp+50h]
    movdqu xmm4,[rsp+60h]
    movdqu xmm5,[rsp+70h]
    movdqu xmm6,[rsp+80h]
    movdqu xmm7,[rsp+90h]
    movdqu xmm8,[rsp+0A0h]
    movdqu xmm9,[rsp+0B0h]
    movdqu xmm10,[rsp+0C0h]
    movdqu xmm11,[rsp+0D0h]
    movdqu xmm12,[rsp+0E0h]
    movdqu xmm13,[rsp+0F0h]
    movdqu xmm14,[rsp+100h]
    movdqu xmm15,[rsp+110h]
    mov rsp,[rsp+120h]
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [CdtFilterTrampoline]
CdtFilterThunk ENDP
END
