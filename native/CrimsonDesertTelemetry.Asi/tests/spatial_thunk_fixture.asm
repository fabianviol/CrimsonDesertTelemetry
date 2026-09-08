EXTERN CdtSpatialThunk:PROC
PUBLIC CdtSpatialTestReturn
.code
CdtSpatialTestInvoke PROC FRAME
    push r13
    .pushreg r13
    sub rsp,20h
    .allocstack 20h
    .endprolog
    mov r13,12345678h
    mov rcx,11h
    mov edx,2
    mov r8d,1
    mov r9d,1
    call CdtSpatialThunk
CdtSpatialTestReturn LABEL BYTE
    cmp r13,12345678h
    sete al
    movzx eax,al
    add rsp,20h
    pop r13
    ret
CdtSpatialTestInvoke ENDP
END
