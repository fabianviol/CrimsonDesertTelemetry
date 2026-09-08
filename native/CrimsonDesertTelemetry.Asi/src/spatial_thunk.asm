; Hook a normal four-argument function ENTRY, not an instruction in its body.
; Preserve the Windows x64 ABI; volatile vector registers already belong to the
; called function. R13 is the exposure caller's actual filter owner.
EXTERN CdtSpatialDispatch:PROC
.code
CdtSpatialThunk PROC FRAME
    sub rsp,38h
    .allocstack 38h
    .endprolog
    mov [rsp+20h],r13
    mov rax,[rsp+38h]
    mov [rsp+28h],rax
    call CdtSpatialDispatch
    add rsp,38h
    ret
CdtSpatialThunk ENDP
END
