        .section	.text.start
        
        # Set stack pointer. The main thread uses a statically-allocated stack
        # in the .data section.

        la	sp, _main_guard
        mv      fp, sp

        # Initalize the trap vector (mtvec CSR). The trap handler is defined
        # in trap.s.

        la      t0, _trap_entry
        csrw    mtvec, t0

        # Arrange it so we call halt_success() if main returns 0, and
        # halt_failure() otherwise.

        call    1f
        bnez    a0, halt_failure
        j       halt_success
1:      j       main

        .section	.data.main_stack
        .align		16
        
        .equ		MAIN_STACK_SIZE, 1024
        .equ		MAIN_GUARD_SIZE, 256

        .global		_main_stack
        .type		_main_stack, @object
        .size		_main_stack, MAIN_STACK_SIZE

        .global		_main_guard
        .type		_main_guard, @object
        .size		_main_guard, MAIN_GUARD_SIZE

_main_stack:
        .fill	MAIN_STACK_SIZE, 1, 0xA5

_main_guard:
        .fill	MAIN_GUARD_SIZE, 1, 0x5A
        .end

